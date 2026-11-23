const { execFile } = require('node:child_process');
const { existsSync, statSync } = require('node:fs');
const path = require('node:path');
const vscode = require('vscode');
const { CliInstaller, cliFileName } = require('./installer');

function settings(folder, installer) {
    const root = folder.uri.fsPath;
    const config = vscode.workspace.getConfiguration('dagdocdiff', folder.uri);
    const source = path.resolve(root, config.get('sourceDir'));
    const build = config.get('buildDir');
    const docs = config.get('docsDir');
    const useBuild = existsSync(path.join(source, 'build', 'compile_commands.json'));
    const buildDir = build ? path.resolve(root, build) : useBuild ? path.join(source, 'build') : source;
    const docsFolder = path.join(source, 'docs');
    const docsDir = docs ? path.resolve(root, docs) : statSync(docsFolder, { throwIfNoEntry: false })?.isDirectory() ? docsFolder : source;
    let executable = config.get('executable');
    if (executable.includes('/') || executable.includes('\\')) {
        executable = path.resolve(root, executable);
    }
    const local = path.join(buildDir, cliFileName);
    executable ||= existsSync(local) ? local : installer.executable;
    return {
        source, buildDir, docsDir,
        executable: executable || cliFileName,
        usePath: !executable,
        configured: Boolean(build) || existsSync(path.join(buildDir, 'compile_commands.json')),
    };
}

function runTool(options, command, args, signal) {
    return new Promise((resolve, reject) => {
        execFile(options.executable, [
            command, '--source-dir', options.source, '--build-dir', options.buildDir, '--docs-dir', options.docsDir, ...args,
        ], { cwd: options.source, signal, maxBuffer: 32 * 1024 * 1024 }, (error, stdout, stderr) => {
            if (!error || typeof error.code === 'number') {
                resolve({ status: error ? error.code : 0, stdout, stderr });
            } else if (error.code === 'ENOENT') {
                reject(new Error('Cannot run ' + options.executable + '. Build Dagdocdiff or set dagdocdiff.executable.', { cause: error }));
            } else {
                reject(error);
            }
        });
    });
}

function readStatus(result) {
    if (result.status === 2 && result.stderr.includes('unknown option --json')) {
        throw new Error('This extension requires a Dagdocdiff build with check --json support.');
    }
    if (result.status > 1 || !result.stdout.trim()) {
        throw new Error(result.stderr.trim().split('\n').at(-1) || 'Dagdocdiff did not return a result.');
    }
    let status;
    try {
        status = JSON.parse(result.stdout);
    } catch {
        throw new Error('Dagdocdiff returned invalid JSON.');
    }
    if (status?.schema_version !== 1 || !Array.isArray(status.pending) || !Array.isArray(status.references) ||
        typeof status.docs_dir !== 'string' || typeof status.source_dir !== 'string') {
        throw new Error('This extension requires a Dagdocdiff build with check --json support.');
    }
    return status;
}

function append(map, key, item) {
    if (!map.has(key)) {
        map.set(key, []);
    }
    map.get(key).push(item);
}

function atLine(number) {
    const position = new vscode.Position(Math.max(0, number - 1), 0);
    return new vscode.Range(position, position);
}

async function openDocument(uri, number) {
    const doc = await vscode.workspace.openTextDocument(uri);
    const line = Math.min(Math.max(1, number), doc.lineCount);
    return vscode.window.showTextDocument(doc, { selection: atLine(line), preview: true });
}

async function pick(items, placeHolder) {
    return items.length === 1 ? items[0] : vscode.window.showQuickPick(items, { placeHolder, matchOnDetail: true });
}

function reviewItem(review) {
    return { label: review.section, description: review.page, detail: review.issues.map(issue => issue.message).join(' • '), review };
}

class Project {
    constructor(folder, changeHints, log, installer) {
        this.folder = folder;
        this.changeHints = changeHints;
        this.log = log;
        this.installer = installer;
        this.diagnostics = vscode.languages.createDiagnosticCollection('dagdocdiff');
        this.abort = new AbortController();
        this.references = new Map();
        this.pages = new Map();
        this.revision = 0;
    }

    schedule(delay = 500) {
        if (this.abort.signal.aborted) {
            return;
        }
        ++this.revision;
        clearTimeout(this.timer);
        this.timer = setTimeout(() => this.refresh(), delay);
    }

    async refresh() {
        if (this.abort.signal.aborted || !vscode.workspace.isTrusted) {
            return;
        }
        if (this.running) {
            this.queued = true;
            return;
        }

        this.running = true;
        const revision = this.revision;
        let options;
        try {
            options = settings(this.folder, this.installer);
            if (!options.configured) {
                this.scm?.dispose();
                this.scm = undefined;
                this.apply(undefined);
                return;
            }
            if (!this.scm) {
                this.scm = vscode.scm.createSourceControl('dagdocdiff', 'Dagdocdiff', this.folder.uri);
                this.scm.inputBox.visible = false;
                this.reviewGroup = this.scm.createResourceGroup('review', 'Needs Review');
                this.brokenGroup = this.scm.createResourceGroup('broken', 'Broken Links');
                this.brokenGroup.hideWhenEmpty = true;
            }
            const result = await runTool(options, 'check', ['--json'], this.abort.signal);
            if (revision !== this.revision || this.abort.signal.aborted) {
                return;
            }
            const status = readStatus(result);
            this.options = options;
            this.lastError = undefined;
            this.apply(status);
        } catch (error) {
            if (revision === this.revision && !this.abort.signal.aborted) {
                this.apply(undefined);
                this.log.appendLine(this.folder.name + ': ' + error.message);
                if (error.cause?.code === 'ENOENT' && options?.usePath && existsSync(options.source) && this.installer.supported) {
                    void this.installer.offer();
                    return;
                }
                if (this.lastError !== error.message) {
                    this.lastError = error.message;
                    void vscode.window.showWarningMessage('Dagdocdiff: ' + error.message, 'Show Log').then(action => {
                        if (action) {
                            this.log.show();
                        }
                    });
                }
            }
        } finally {
            this.running = false;
            if (this.queued && !this.abort.signal.aborted) {
                this.queued = false;
                clearTimeout(this.timer);
                void this.refresh();
            }
        }
    }

    apply(status) {
        this.docsDir = status?.docs_dir;
        this.references.clear();
        this.pages.clear();
        this.diagnostics.clear();
        if (status) {
            const referenceSections = new Map();
            for (const reference of status.references) {
                const file = path.join(status.source_dir, reference.file);
                append(this.references, file, reference);
                append(referenceSections, reference.page + '\0' + reference.section, { reference, file });
            }

            const diagnostics = new Map();
            for (const review of status.pending) {
                const uri = this.pageUri(review.page);
                append(this.pages, uri.fsPath, review);
                for (const issue of review.issues) {
                    const diagnostic = new vscode.Diagnostic(atLine(issue.line), issue.message,
                        review.approvable ? vscode.DiagnosticSeverity.Warning : vscode.DiagnosticSeverity.Error);
                    diagnostic.source = 'Dagdocdiff';
                    const references = referenceSections.get(review.page + '\0' + review.section) || [];
                    diagnostic.relatedInformation = references
                        .filter(({ reference }) => issue.line === review.line || issue.line === reference.line)
                        .map(({ reference, file }) => new vscode.DiagnosticRelatedInformation(
                            new vscode.Location(vscode.Uri.file(file), atLine(reference.first_line)), reference.text));
                    append(diagnostics, uri.fsPath, diagnostic);
                }
            }
            for (const [file, issues] of diagnostics) {
                this.diagnostics.set(vscode.Uri.file(file), issues);
            }
        }

        if (this.scm) {
            this.scm.count = status?.pending.length || 0;
            this.reviewGroup.resourceStates = this.resourceStates(true);
            this.brokenGroup.resourceStates = this.resourceStates(false);
        }
        this.changeHints.fire();
    }

    resourceStates(approvable) {
        const resources = [];
        for (const [file, reviews] of this.pages) {
            const entries = reviews.filter(review => review.approvable === approvable);
            if (!entries.length) {
                continue;
            }
            resources.push({
                resourceUri: vscode.Uri.file(file),
                decorations: {
                    tooltip: entries.flatMap(review => review.issues.map(issue => issue.message)).join('\n'),
                    strikeThrough: entries.every(review => review.line === 0),
                },
                command: {
                    command: 'dagdocdiff.openReview', title: 'Open Documentation Review',
                    arguments: [this.folder.uri.toString(), entries],
                },
            });
        }
        return resources;
    }

    pageUri(page) {
        return vscode.Uri.file(path.join(this.docsDir, page));
    }

    async openReview(reviews) {
        const item = await pick(reviews.map(reviewItem), 'Select a documentation section to review');
        if (item && this.docsDir) {
            if (item.review.line === 0 && !existsSync(this.pageUri(item.review.page).fsPath)) {
                await vscode.window.showInformationMessage(item.detail);
                return;
            }
            await openDocument(this.pageUri(item.review.page), item.review.line);
        }
    }

    async approve(file) {
        if (this.approving || this.abort.signal.aborted || !vscode.workspace.isTrusted) {
            return;
        }
        const reviews = this.pages.get(file) || [];
        const eligible = reviews.filter(review => review.approvable);
        if (!eligible.length) {
            return;
        }
        this.approving = true;
        try {
            const items = eligible.map(reviewItem);
            if (eligible.length > 1 && eligible.length === reviews.length) {
                items.unshift({ label: 'Approve whole file', description: eligible[0].page });
            }
            const item = await pick(items, 'Approve the documentation you reviewed');
            if (!item || this.abort.signal.aborted) {
                return;
            }
            const args = ['--', eligible[0].page];
            if (item.review) {
                args.push(item.review.section);
            }
            const result = await runTool(this.options, 'approve', args, this.abort.signal);
            if (result.status !== 0) {
                throw new Error(result.stderr.trim().split('\n').at(-1) || 'Could not approve documentation.');
            }
            this.log.appendLine(result.stdout.trim());
        } finally {
            this.approving = false;
            this.schedule(0);
        }
    }

    dispose() {
        this.abort.abort();
        clearTimeout(this.timer);
        this.scm?.dispose();
        this.diagnostics.dispose();
        this.changeHints.fire();
    }
}

function activate(context) {
    const projects = new Map();
    const changeHints = new vscode.EventEmitter();
    const log = vscode.window.createOutputChannel('Dagdocdiff');
    const projectFor = uri => {
        if (!uri) {
            return undefined;
        }
        const folder = vscode.workspace.getWorkspaceFolder(uri);
        return (folder && projects.get(folder.uri.toString())) ||
            [...projects.values()].find(project => project.pages.has(uri.fsPath) || project.references.has(uri.fsPath));
    };
    const add = folder => {
        if (folder.uri.scheme === 'file') {
            const project = new Project(folder, changeHints, log, installer);
            projects.set(folder.uri.toString(), project);
            project.schedule(0);
        }
    };
    const scheduleAll = () => {
        for (const project of projects.values()) {
            project.schedule(0);
        }
    };
    const installer = new CliInstaller(context, log, scheduleAll);
    const reportError = async work => {
        try {
            await work();
        } catch (error) {
            log.appendLine(error.message);
            await vscode.window.showErrorMessage('Dagdocdiff: ' + error.message);
        }
    };
    const watcher = vscode.workspace.createFileSystemWatcher('**/*.{c,cc,cpp,cxx,C,h,hh,hpp,hxx,H,inc,ipp,tpp,md,markdown,json}');
    const fileChanged = uri => projectFor(uri)?.schedule();
    context.subscriptions.push(
        { dispose: () => { for (const project of projects.values()) { project.dispose(); } } },
        changeHints, log, watcher, installer,
        watcher.onDidCreate(fileChanged), watcher.onDidChange(fileChanged), watcher.onDidDelete(fileChanged),
        vscode.workspace.onDidSaveTextDocument(document => fileChanged(document.uri)),
        vscode.workspace.onDidChangeConfiguration(event => {
            if (event.affectsConfiguration('dagdocdiff')) {
                scheduleAll();
            }
        }),
        vscode.workspace.onDidChangeWorkspaceFolders(event => {
            for (const folder of event.removed) {
                const key = folder.uri.toString();
                projects.get(key)?.dispose();
                projects.delete(key);
            }
            event.added.forEach(add);
        }),
        vscode.workspace.onDidGrantWorkspaceTrust(scheduleAll),
        vscode.commands.registerCommand('dagdocdiff.refresh', scheduleAll),
        vscode.commands.registerCommand('dagdocdiff.install', () => installer.install()),
        vscode.commands.registerCommand('dagdocdiff.openReview', (key, reviews) =>
            reportError(() => projects.get(key)?.openReview(reviews))),
        vscode.commands.registerCommand('dagdocdiff.approve', (...resources) => reportError(async () => {
            const files = resources.map(resource => resource?.resourceUri).filter(Boolean);
            if (!files.length) {
                const uri = vscode.window.activeTextEditor?.document.uri;
                if (uri) {
                    files.push(uri);
                }
            }
            for (const uri of files) {
                await projectFor(uri)?.approve(uri.fsPath);
            }
        })),
        vscode.commands.registerCommand('dagdocdiff.openReferences', (key, references) => reportError(async () => {
            const project = projects.get(key);
            if (!project?.docsDir) {
                return;
            }
            const items = references.map(reference => ({
                label: reference.section, description: reference.page,
                detail: reference.text + ' (line ' + reference.line + ')', reference,
            }));
            const item = await pick(items, 'Select a referring documentation link');
            if (item) {
                await openDocument(project.pageUri(item.reference.page), item.reference.line);
            }
        })),
        vscode.languages.registerInlayHintsProvider({ scheme: 'file' }, {
            onDidChangeInlayHints: changeHints.event,
            provideInlayHints(document, range) {
                const project = projectFor(document.uri);
                const references = project?.references.get(document.uri.fsPath);
                if (!references || document.isDirty) {
                    return [];
                }
                const lines = new Map();
                for (const reference of references) {
                    const first = Math.max(reference.first_line - 1, range.start.line);
                    const last = Math.min(reference.last_line - 1, range.end.line, document.lineCount - 1);
                    for (let line = first; line <= last; ++line) {
                        append(lines, line, reference);
                    }
                }
                const hints = [];
                for (const [line, matches] of lines) {
                    const position = document.lineAt(line).range.end;
                    if (!range.contains(position)) {
                        continue;
                    }
                    const part = new vscode.InlayHintLabelPart('↗');
                    part.tooltip = 'Open referring documentation';
                    part.command = {
                        command: 'dagdocdiff.openReferences', title: 'Open Referring Documentation',
                        arguments: [project.folder.uri.toString(), matches],
                    };
                    const hint = new vscode.InlayHint(position, [part]);
                    hint.paddingLeft = true;
                    hints.push(hint);
                }
                return hints;
            },
        }),
    );

    (vscode.workspace.workspaceFolders || []).forEach(add);
}

module.exports = { activate };

const { execFile } = require('node:child_process');
const { existsSync, statSync } = require('node:fs');
const path = require('node:path');
const vscode = require('vscode');
const { CliInstaller, cliFileName } = require('./installer');

function settings(folder, installer) {
    const root = folder.uri.fsPath;
    const config = vscode.workspace.getConfiguration('c-docc', folder.uri);
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
                reject(new Error('Cannot run ' + options.executable + '. Build c-docc or set c-docc.executable.', { cause: error }));
            } else {
                reject(error);
            }
        });
    });
}

function readStatus(result) {
    if (result.status === 2 && result.stderr.includes('unknown option --json')) {
        throw new Error('This extension requires a c-docc build with check --json support.');
    }
    if (result.status > 1 || !result.stdout.trim()) {
        throw new Error(result.stderr.trim().split('\n').at(-1) || 'c-docc did not return a result.');
    }
    let status;
    try {
        status = JSON.parse(result.stdout);
    } catch {
        throw new Error('c-docc returned invalid JSON.');
    }
    if (status?.schema_version !== 1 || !Array.isArray(status.pending) || !Array.isArray(status.references) ||
        typeof status.docs_dir !== 'string' || typeof status.source_dir !== 'string') {
        throw new Error('This extension requires a c-docc build with check --json support.');
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

class Project {
    constructor(folder, changeNavigation, log, installer) {
        this.folder = folder;
        this.changeNavigation = changeNavigation;
        this.log = log;
        this.installer = installer;
        this.diagnostics = vscode.languages.createDiagnosticCollection('c-docc');
        this.abort = new AbortController();
        this.references = new Map();
        this.pageReferences = new Map();
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
                this.scm = vscode.scm.createSourceControl('c-docc', 'c-docc', this.folder.uri);
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
                    void vscode.window.showWarningMessage('c-docc: ' + error.message, 'Show Log').then(action => {
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
        this.sourceDir = status?.source_dir;
        this.references.clear();
        this.pageReferences.clear();
        this.pages.clear();
        this.diagnostics.clear();
        if (status) {
            for (const reference of status.references) {
                append(this.references, this.sourceUri(reference.file).fsPath, reference);
                append(this.pageReferences, this.pageUri(reference.page).fsPath, reference);
            }
            for (const reference of status.file_references || []) {
                append(this.pageReferences, this.pageUri(reference.page).fsPath, reference);
            }

            const diagnostics = new Map();
            for (const review of status.pending) {
                const uri = this.pageUri(review.page);
                append(this.pages, uri.fsPath, review);
                for (const issue of review.issues) {
                    const diagnostic = new vscode.Diagnostic(atLine(issue.line), issue.message,
                        review.approvable ? vscode.DiagnosticSeverity.Warning : vscode.DiagnosticSeverity.Error);
                    diagnostic.source = 'c-docc';
                    const references = this.pageReferences.get(uri.fsPath) || [];
                    const related = references
                        .filter(reference => reference.section === review.section &&
                            (issue.line === review.line || issue.line === reference.line))
                        .map(reference => new vscode.DiagnosticRelatedInformation(
                            new vscode.Location(this.sourceUri(reference.file), atLine(reference.first_line)), reference.text));
                    if (issue.cause) {
                        related.unshift(new vscode.DiagnosticRelatedInformation(
                            new vscode.Location(this.sourceUri(issue.cause.file), atLine(issue.cause.line)), 'Changed definition'));
                    }
                    diagnostic.relatedInformation = related;
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
        this.changeNavigation.fire();
    }

    resourceStates(approvable) {
        const resources = [];
        for (const [file, reviews] of this.pages) {
            if (reviews.every(review => review.approvable) !== approvable) {
                continue;
            }
            resources.push({
                resourceUri: vscode.Uri.file(file),
                decorations: {
                    tooltip: reviews.flatMap(review => review.issues.map(issue => issue.message)).join('\n'),
                    strikeThrough: reviews.every(review => review.line === 0),
                },
                command: {
                    command: 'c-docc.openReview', title: 'Open Documentation Review',
                    arguments: [this.folder.uri.toString(), reviews],
                },
            });
        }
        return resources;
    }

    pageUri(page) {
        return vscode.Uri.file(path.join(this.docsDir, page));
    }

    sourceUri(file) {
        return vscode.Uri.file(path.join(this.sourceDir, file));
    }

    async openReview(reviews) {
        const review = reviews.find(entry => entry.line > 0) || reviews[0];
        if (!review || !this.docsDir) {
            return;
        }
        const uri = this.pageUri(review.page);
        if (!existsSync(uri.fsPath)) {
            for (const entry of reviews) {
                for (const issue of entry.issues) {
                    this.log.appendLine(entry.page + ' / ' + entry.section + ': ' + issue.message);
                }
            }
            this.log.show();
            return;
        }
        const line = Math.min(...review.issues.map(issue => issue.line || review.line || 1));
        await openDocument(uri, line);
    }

    async approve(file) {
        const reviews = this.pages.get(file);
        if (!reviews?.length || reviews.some(review => !review.approvable)) {
            return;
        }
        await this.writeReview('approve', reviews[0].page);
    }

    async writeReview(command, target) {
        if (!this.options || this.writingReview || this.abort.signal.aborted || !vscode.workspace.isTrusted) {
            return;
        }
        this.writingReview = true;
        try {
            const result = await runTool(this.options, command, ['--', target], this.abort.signal);
            if (result.status !== 0) {
                throw new Error(result.stderr.trim().split('\n').at(-1) || 'Could not ' + command + ' documentation.');
            }
            this.log.appendLine(result.stdout.trim());
        } finally {
            this.writingReview = false;
            this.schedule(0);
        }
    }

    dispose() {
        this.abort.abort();
        clearTimeout(this.timer);
        this.scm?.dispose();
        this.diagnostics.dispose();
        this.changeNavigation.fire();
    }
}

function activate(context) {
    const projects = new Map();
    const changeNavigation = new vscode.EventEmitter();
    const log = vscode.window.createOutputChannel('c-docc');
    const projectFor = uri => {
        if (!uri) {
            return undefined;
        }
        const folder = vscode.workspace.getWorkspaceFolder(uri);
        return (folder && projects.get(folder.uri.toString())) ||
            [...projects.values()].find(project => project.pages.has(uri.fsPath) ||
                project.pageReferences.has(uri.fsPath) || project.references.has(uri.fsPath));
    };
    const backlinkLenses = new WeakMap();
    const add = folder => {
        if (folder.uri.scheme === 'file') {
            const project = new Project(folder, changeNavigation, log, installer);
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
            await vscode.window.showErrorMessage('c-docc: ' + error.message);
        }
    };
    const watcher = vscode.workspace.createFileSystemWatcher('**/*.{c,cc,cpp,cxx,C,h,hh,hpp,hxx,H,inc,ipp,tpp,md,markdown,json}');
    const fileChanged = uri => projectFor(uri)?.schedule();
    context.subscriptions.push(
        { dispose: () => { for (const project of projects.values()) { project.dispose(); } } },
        changeNavigation, log, watcher, installer,
        watcher.onDidCreate(fileChanged), watcher.onDidChange(fileChanged), watcher.onDidDelete(fileChanged),
        vscode.workspace.onDidSaveTextDocument(document => fileChanged(document.uri)),
        vscode.workspace.onDidChangeConfiguration(event => {
            if (event.affectsConfiguration('c-docc')) {
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
        vscode.commands.registerCommand('c-docc.refresh', scheduleAll),
        vscode.commands.registerCommand('c-docc.install', () => installer.install()),
        vscode.commands.registerCommand('c-docc.openReview', (key, reviews) =>
            reportError(() => projects.get(key)?.openReview(reviews))),
        vscode.commands.registerCommand('c-docc.approve', (...resources) => reportError(async () => {
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
        vscode.commands.registerCommand('c-docc.invalidate', uri => reportError(async () => {
            const target = uri || vscode.window.activeTextEditor?.document.uri;
            await projectFor(target)?.writeReview('invalidate', target.fsPath);
        })),
        vscode.commands.registerCommand('c-docc.openReferences', (key, references) => reportError(async () => {
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
        vscode.languages.registerCodeLensProvider({ scheme: 'file' }, {
            onDidChangeCodeLenses: changeNavigation.event,
            provideCodeLenses(document) {
                const project = projectFor(document.uri);
                const references = project?.references.get(document.uri.fsPath);
                if (!references || document.isDirty) {
                    return [];
                }

                const lines = new Map();
                for (const reference of references) {
                    const last = Math.min(reference.last_line, document.lineCount);
                    for (let line = reference.first_line; line <= last; ++line) {
                        append(lines, line, reference);
                    }
                }
                return Array.from(lines, ([line, matches]) => {
                    const lens = new vscode.CodeLens(atLine(line));
                    backlinkLenses.set(lens, { project, matches });
                    return lens;
                });
            },
            async resolveCodeLens(lens, token) {
                const { project, matches } = backlinkLenses.get(lens);
                const previews = await Promise.all(matches.map(async reference => {
                    try {
                        const page = await vscode.workspace.openTextDocument(project.pageUri(reference.page));
                        if (reference.line <= page.lineCount) {
                            return page.lineAt(reference.line - 1).text;
                        }
                    } catch {
                        // The page can move before the next check refreshes its references.
                    }
                }));
                if (token.isCancellationRequested) {
                    return lens;
                }
                const [reference] = matches;
                const title = matches.length === 1
                    ? 'Open ' + path.basename(reference.page) + ':' + reference.line
                    : 'Open ' + matches.length + ' documentation references';
                lens.command = {
                    command: 'c-docc.openReferences',
                    title,
                    tooltip: [...new Set(previews.filter(Boolean))].join('\n\n'),
                    arguments: [project.folder.uri.toString(), matches],
                };
                return lens;
            },
        }),
    );

    (vscode.workspace.workspaceFolders || []).forEach(add);
}

module.exports = { activate };

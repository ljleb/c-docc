const { execFile } = require('node:child_process');
const { existsSync } = require('node:fs');
const { mkdir } = require('node:fs/promises');
const path = require('node:path');
const { promisify } = require('node:util');
const vscode = require('vscode');
const { cDoccFlake, cDoccWindowsRelease, version } = require('./package.json');
const { installWindowsCli } = require('./windows-installer');

const exec = promisify(execFile);
const cliFileName = process.platform === 'win32' ? 'c-docc.exe' : 'c-docc';

class CliInstaller {
    constructor(context, log, refresh) {
        this.storage = context.globalStorageUri.fsPath;
        this.windowsHost = process.platform === 'win32' && process.arch === 'x64';
        this.nixHost = process.platform === 'linux' && ['x64', 'arm64'].includes(process.arch);
        this.windows = this.windowsHost && Boolean(cDoccWindowsRelease);
        this.nix = this.nixHost && Boolean(cDoccFlake);
        const bundle = this.windows ? 'c-docc-' + version + '-x86_64-windows' : 'cli';
        this.installDir = path.join(this.storage, bundle);
        this.binary = path.join(this.installDir, 'bin', cliFileName);
        this.supported = this.windows || this.nix;
        this.abort = new AbortController();
        this.log = log;
        this.refresh = refresh;
    }

    get executable() {
        return existsSync(this.binary) ? this.binary : undefined;
    }

    async offer() {
        if (this.prompted || this.installing || this.abort.signal.aborted) {
            return;
        }
        this.prompted = true;
        const install = this.windows ? 'Install c-docc' : 'Install with Nix';
        const message = this.windows ? 'Download it from GitHub?' : 'Install it with Nix?';
        const action = await vscode.window.showInformationMessage('c-docc CLI was not found. ' + message, install, 'Later');
        if (action === install) {
            await this.install();
        }
    }

    async install() {
        if (this.installing || this.abort.signal.aborted || !vscode.workspace.isTrusted) {
            return;
        }
        if (!this.supported) {
            const message = this.windowsHost || this.nixHost
                ? 'Install the c-docc VSIX from a GitHub release to enable CLI installation.'
                : 'Automatic c-docc installation supports x64 Windows and x64 or ARM64 Linux.';
            await vscode.window.showInformationMessage(message);
            return;
        }
        if (this.windows && this.executable) {
            this.refresh();
            await vscode.window.showInformationMessage('c-docc CLI is already installed.');
            return;
        }
        this.installing = true;
        try {
            await vscode.window.withProgress({
                location: vscode.ProgressLocation.Notification,
                title: this.windows ? 'Installing the c-docc CLI' : 'Installing the c-docc CLI with Nix',
                cancellable: true,
            }, async (progress, cancellation) => {
                const cancel = new AbortController();
                const listener = cancellation.onCancellationRequested(() => cancel.abort());
                const signal = AbortSignal.any([this.abort.signal, cancel.signal]);
                try {
                    await mkdir(this.storage, { recursive: true });
                    if (this.windows) {
                        this.log.appendLine('Installing c-docc from ' + cDoccWindowsRelease);
                        await installWindowsCli(cDoccWindowsRelease, version, this.installDir, signal, progress);
                    } else {
                        await this.installWithNix(signal);
                    }
                } catch (error) {
                    if (signal.aborted) {
                        return;
                    }
                    throw error;
                } finally {
                    listener.dispose();
                }
                this.refresh();
                void vscode.window.showInformationMessage('c-docc CLI installed.');
            });
        } catch (error) {
            this.log.appendLine(error.message);
            const message = this.nix && error.code === 'ENOENT'
                ? 'c-docc installation needs Nix on PATH where the extension runs.'
                : 'c-docc CLI installation failed.';
            const action = await vscode.window.showErrorMessage(message, 'Show Log');
            if (action === 'Show Log') {
                this.log.show();
            }
        } finally {
            this.installing = false;
        }
    }

    async installWithNix(signal) {
        this.log.appendLine('Installing c-docc from ' + cDoccFlake);
        const child = exec('nix', [
            '--extra-experimental-features', 'nix-command flakes',
            'build', '--no-write-lock-file', '--out-link', this.installDir, cDoccFlake,
        ], { cwd: this.storage, signal });
        child.child.stderr?.on('data', message => this.log.append(message));
        await child;
        if (!this.executable) {
            throw new Error('The Nix package did not contain the c-docc CLI.');
        }
    }

    dispose() {
        this.abort.abort();
    }
}

module.exports = { CliInstaller, cliFileName };

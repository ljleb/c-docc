const { execFile } = require('node:child_process');
const { existsSync } = require('node:fs');
const { mkdir } = require('node:fs/promises');
const path = require('node:path');
const { promisify } = require('node:util');
const vscode = require('vscode');
const { dagdocdiffFlake, dagdocdiffWindowsRelease, version } = require('./package.json');
const { installWindowsCli } = require('./windows-installer');

const exec = promisify(execFile);
const cliFileName = process.platform === 'win32' ? 'dagdocdiff.exe' : 'dagdocdiff';

class CliInstaller {
    constructor(context, log, refresh) {
        this.storage = context.globalStorageUri.fsPath;
        this.windows = process.platform === 'win32' && process.arch === 'x64' && Boolean(dagdocdiffWindowsRelease);
        this.nix = process.platform === 'linux' && ['x64', 'arm64'].includes(process.arch);
        const bundle = this.windows ? 'dagdocdiff-' + version + '-x86_64-windows' : 'cli';
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
        const install = this.windows ? 'Install Dagdocdiff' : 'Install with Nix';
        const message = this.windows ? 'Download it from GitHub?' : 'Install it with Nix?';
        const action = await vscode.window.showInformationMessage('Dagdocdiff CLI was not found. ' + message, install, 'Later');
        if (action === install) {
            await this.install();
        }
    }

    async install() {
        if (this.installing || this.abort.signal.aborted || !vscode.workspace.isTrusted) {
            return;
        }
        if (!this.supported) {
            const message = process.platform === 'win32' && process.arch === 'x64'
                ? 'Install the Dagdocdiff VSIX from a GitHub release to enable CLI downloads.'
                : 'Automatic Dagdocdiff installation supports x64 Windows and x64 or ARM64 Linux.';
            await vscode.window.showInformationMessage(message);
            return;
        }
        if (this.windows && this.executable) {
            this.refresh();
            await vscode.window.showInformationMessage('Dagdocdiff CLI is already installed.');
            return;
        }
        this.installing = true;
        try {
            await vscode.window.withProgress({
                location: vscode.ProgressLocation.Notification,
                title: this.windows ? 'Installing the Dagdocdiff CLI' : 'Installing the Dagdocdiff CLI with Nix',
                cancellable: true,
            }, async (progress, cancellation) => {
                const cancel = new AbortController();
                const listener = cancellation.onCancellationRequested(() => cancel.abort());
                const signal = AbortSignal.any([this.abort.signal, cancel.signal]);
                try {
                    await mkdir(this.storage, { recursive: true });
                    if (this.windows) {
                        this.log.appendLine('Installing Dagdocdiff from ' + dagdocdiffWindowsRelease);
                        await installWindowsCli(dagdocdiffWindowsRelease, version, this.installDir, signal, progress);
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
                void vscode.window.showInformationMessage('Dagdocdiff CLI installed.');
            });
        } catch (error) {
            this.log.appendLine(error.message);
            const message = this.nix && error.code === 'ENOENT'
                ? 'Dagdocdiff installation needs Nix on PATH where the extension runs.'
                : 'Dagdocdiff CLI installation failed.';
            const action = await vscode.window.showErrorMessage(message, 'Show Log');
            if (action === 'Show Log') {
                this.log.show();
            }
        } finally {
            this.installing = false;
        }
    }

    async installWithNix(signal) {
        this.log.appendLine('Installing Dagdocdiff from ' + dagdocdiffFlake);
        const child = exec('nix', [
            '--extra-experimental-features', 'nix-command flakes',
            'build', '--no-write-lock-file', '--out-link', this.installDir, dagdocdiffFlake,
        ], { cwd: this.storage, signal });
        child.child.stderr?.on('data', message => this.log.append(message));
        await child;
        if (!this.executable) {
            throw new Error('The Nix package did not contain the Dagdocdiff CLI.');
        }
    }

    dispose() {
        this.abort.abort();
    }
}

module.exports = { CliInstaller, cliFileName };

const { execFile } = require('node:child_process');
const { createHash } = require('node:crypto');
const { createReadStream, createWriteStream, existsSync } = require('node:fs');
const { mkdtemp, rename, rm } = require('node:fs/promises');
const path = require('node:path');
const { pipeline } = require('node:stream/promises');
const { promisify } = require('node:util');

const exec = promisify(execFile);

async function fetchAsset(release, asset, signal) {
    const response = await fetch(release + '/' + asset, { signal });
    if (!response.ok || !response.body) {
        await response.body?.cancel();
        throw new Error('Could not download ' + asset + ' (HTTP ' + response.status + ').');
    }
    return response;
}

async function installWindowsCli(release, version, destination, signal, progress) {
    const bundle = path.basename(destination);
    const asset = bundle + '.zip';
    const checksums = await fetchAsset(release, 'SHA256SUMS', signal);
    const match = (await checksums.text()).split(/\r?\n/)
        .map(line => /^([a-f0-9]{64}) [ *](.+)$/i.exec(line))
        .find(entry => entry?.[2] === asset);
    if (!match) {
        throw new Error('The release has no SHA-256 checksum for ' + asset + '.');
    }

    const staging = await mkdtemp(path.join(path.dirname(destination), 'download-'));
    try {
        progress.report({ message: 'Downloading ' + asset });
        const archive = path.join(staging, asset);
        const response = await fetchAsset(release, asset, signal);
        await pipeline(response.body, createWriteStream(archive), { signal });

        progress.report({ message: 'Verifying the download' });
        const hash = createHash('sha256');
        for await (const chunk of createReadStream(archive, { signal })) {
            hash.update(chunk);
        }
        if (hash.digest('hex') !== match[1].toLowerCase()) {
            throw new Error('The SHA-256 checksum did not match ' + asset + '.');
        }

        progress.report({ message: 'Extracting the CLI' });
        const tar = path.join(process.env.SystemRoot || 'C:\\Windows', 'System32', 'tar.exe');
        const options = { cwd: staging, signal, windowsHide: true };
        await exec(tar, ['-xf', archive], options);
        const extracted = path.join(staging, bundle);
        const { stdout } = await exec(path.join(extracted, 'bin', 'dagdocdiff.exe'), ['--version'], options);
        if (stdout.trim() !== 'dagdocdiff ' + version) {
            throw new Error('The Windows ZIP contains a different Dagdocdiff version.');
        }

        signal.throwIfAborted();
        try {
            await rename(extracted, destination);
        } catch (error) {
            // Another editor window can finish installing the same release first.
            if (!existsSync(path.join(destination, 'bin', 'dagdocdiff.exe'))) {
                throw error;
            }
        }
    } finally {
        await rm(staging, { recursive: true, force: true, maxRetries: 3 });
    }
}

module.exports = { installWindowsCli };

const { copyFileSync, mkdirSync, readFileSync, writeFileSync } = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '../..');
const version = readFileSync(path.join(root, 'VERSION'), 'utf8').trim();
if (!/^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$/.test(version)) {
    throw new Error('VERSION must contain a release version such as 1.2.3.');
}

const manifest = require('./package.json');
const repository = new URL(manifest.repository.url);
const repo = process.env.GITHUB_REPOSITORY || repository.pathname.slice(1).replace(/\.git$/, '');
const server = process.env.GITHUB_SERVER_URL || repository.origin;
const ref = process.env.GITHUB_SHA || version;
manifest.version = version;
manifest.cDoccFlake = 'github:' + repo + '/' + ref;
manifest.cDoccWindowsRelease = server + '/' + repo + '/releases/download/' + version;
delete manifest.scripts;

const output = path.resolve(process.argv[2]);
mkdirSync(output, { recursive: true });
for (const file of ['README.md', ...manifest.files]) {
    const source = path.join(file === 'LICENSE' ? root : __dirname, file);
    copyFileSync(source, path.join(output, file));
}
writeFileSync(path.join(output, 'package.json'), JSON.stringify(manifest, null, 2) + '\n');

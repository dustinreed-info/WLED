'use strict';

const assert = require('node:assert/strict');
const { it } = require('node:test');
const { spawnSync } = require('node:child_process');
const path = require('node:path');

it('firmware revision metadata handles clean, dirty and unavailable Git repositories', t => {
  const python = process.env.PYTHON || 'python3';
  const probe = spawnSync(python, ['--version'], { encoding: 'utf8' });
  if (probe.error?.code === 'ENOENT') return t.skip('Python is unavailable');
  assert.equal(probe.status, 0, probe.stderr);
  const script = String.raw`
import ast, pathlib, re, subprocess, sys
from types import SimpleNamespace
tree = ast.parse(pathlib.Path(sys.argv[1]).read_text())
functions = [n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name in {'get_git_revision', 'has_def', 'add_wled_metadata_flags'}]
assert len(functions) == 3
ns = {'re': re, 'WLED_VERSION': '17.0.0-devV5', 'get_github_repo': lambda: 'owner/repo'}
exec(compile(ast.Module(body=functions, type_ignores=[]), '<production metadata functions>', 'exec'), ns)
revision = '0123456789ab'
def install_git(value=revision, status=0, error=None):
    def run(args, **kwargs):
        if error:
            raise error
        if args[1] == 'rev-parse':
            assert kwargs['check'] and kwargs['text']
            return SimpleNamespace(stdout=value+'\n', returncode=0)
        assert args == ['git', 'diff', '--quiet', 'HEAD', '--']
        return SimpleNamespace(stdout='', returncode=status)
    ns['subprocess'] = SimpleNamespace(run=run, CalledProcessError=subprocess.CalledProcessError)
install_git(); assert ns['get_git_revision']() == revision
install_git(status=1); assert ns['get_git_revision']() == revision+'-dirty'
install_git(status=2); assert ns['get_git_revision']() is None
install_git(value='invalid;macro'); assert ns['get_git_revision']() is None
install_git(error=FileNotFoundError()); assert ns['get_git_revision']() is None
install_git(error=subprocess.CalledProcessError(128, ['git'])); assert ns['get_git_revision']() is None
class Env(dict):
    def Object(self, node, **kwargs):
        return kwargs['CPPDEFINES']
install_git()
env = Env(CPPDEFINES=[])
definitions = dict(ns['add_wled_metadata_flags'](env, 'metadata.cpp'))
assert revision in definitions['WLED_GIT_REVISION']
assert 'owner/repo' in definitions['WLED_REPO']
assert env['CPPDEFINES'] == []
ns['get_git_revision'] = lambda: (_ for _ in ()).throw(AssertionError('override must not query Git'))
env = Env(CPPDEFINES=[('WLED_GIT_REVISION', 'custom')])
definitions = dict(ns['add_wled_metadata_flags'](env, 'metadata.cpp'))
assert definitions['WLED_GIT_REVISION'] == 'custom'
print('8 metadata scenarios passed')
`;
  const result = spawnSync(python, ['-c', script, path.resolve(__dirname, '../pio-scripts/set_metadata.py')], { encoding: 'utf8' });
  assert.equal(result.status, 0, `${result.stdout}\n${result.stderr}`);
  t.diagnostic(result.stdout.trim());
});

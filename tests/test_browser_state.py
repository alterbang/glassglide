from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


def compile_local_pages(directory):
    harness = Path(directory) / "local-pages-test"
    subprocess.run(
        ["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
         "-I", str(ROOT / "src"), str(ROOT / "tests/local_pages_harness.cpp"),
         str(ROOT / "src/browser_state.cpp"), str(ROOT / "src/local_pages.cpp"),
         "-o", str(harness)],
        check=True, capture_output=True, text=True,
    )
    return harness


class BrowserStateTest(unittest.TestCase):
    def test_navigation_private_tabs_and_persistent_storage(self):
        with tempfile.TemporaryDirectory(prefix="glassglide-browser-test-") as directory:
            harness = Path(directory) / "browser-state-test"
            subprocess.run(
                ["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                 "-I", str(ROOT / "src"), str(ROOT / "tests/browser_state_harness.cpp"),
                 str(ROOT / "src/browser_state.cpp"), str(ROOT / "src/browser_store.cpp"),
                 "-o", str(harness)],
                check=True, capture_output=True, text=True,
            )
            result = subprocess.run([str(harness), directory], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("Browser state, privacy and durable storage: OK", result.stdout)

    def test_browser_controls_and_blocked_addresses(self):
        with tempfile.TemporaryDirectory(prefix="glassglide-ui-test-") as directory:
            harness = Path(directory) / "browser-ui-test"
            subprocess.run(
                ["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                 "-I", str(ROOT / "src"), str(ROOT / "tests/browser_ui_harness.cpp"),
                 str(ROOT / "src/browser_state.cpp"), str(ROOT / "src/browser_ui.cpp"),
                 "-o", str(harness)],
                check=True, capture_output=True, text=True,
            )
            result = subprocess.run([str(harness)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("Browser controls, blocked URLs and private data: OK", result.stdout)

    def test_local_pages_and_navigation_policy(self):
        with tempfile.TemporaryDirectory(prefix="glassglide-pages-test-") as directory:
            harness = compile_local_pages(directory)
            result = subprocess.run([str(harness)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("Local pages and navigation policy: OK", result.stdout)

    @unittest.skipUnless(shutil.which("node"), "Node.js is required for the local JavaScript test")
    def test_demo_page_javascript(self):
        with tempfile.TemporaryDirectory(prefix="glassglide-javascript-test-") as directory:
            harness = compile_local_pages(directory)
            html = subprocess.check_output([str(harness), "--demo-html"], text=True)
            script = r"""
const assert = require('node:assert/strict');
const vm = require('node:vm');
const html = require('node:fs').readFileSync(0, 'utf8');
const javascript = html.match(/<script>([\s\S]*?)<\/script>/)[1];
const initial = html.match(/<p id="input">([^<]+)<\/p>/)[1];
const listeners = {};
const status = { textContent: initial };
const counter = {
  textContent: 'Prova il pulsante: 0',
  addEventListener: (name, callback) => { listeners[name] = callback; },
  focus: () => { document.activeElement = counter; }
};
const document = {
  activeElement: null,
  getElementById: id => id === 'input' ? status : counter,
  addEventListener: (name, callback) => { listeners[name] = callback; },
  querySelectorAll: () => [counter]
};
const scrolls = [];
const window = { scrollBy: (x, y) => scrolls.push([x, y]) };
assert.match(status.textContent, /non ancora attivo/);
vm.runInNewContext(javascript, { document, window });
assert.match(status.textContent, /JavaScript attivo/);
listeners.click();
listeners.click();
assert.equal(counter.textContent, 'Prova il pulsante: 2');
let prevented = false;
listeners.keydown({ key: 'ArrowDown', preventDefault: () => { prevented = true; } });
assert.equal(status.textContent, 'Tasto ricevuto: ArrowDown');
assert.equal(scrolls.length, 1);
assert.equal(scrolls[0][1], 140);
assert(prevented);
listeners.keydown({ key: 'ArrowRight', preventDefault: () => {} });
assert.equal(document.activeElement, counter);
"""
            result = subprocess.run([shutil.which("node"), "-e", script], input=html,
                                    text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()

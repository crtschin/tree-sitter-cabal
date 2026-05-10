import { test } from "node:test";
import assert from "node:assert";
import { readFileSync, readdirSync } from "node:fs";
import { join, dirname, relative } from "node:path";
import { fileURLToPath } from "node:url";
import Parser from "tree-sitter";
import language from "../bindings/node/index.js";

const cabalDir = join(
  dirname(fileURLToPath(import.meta.url)),
  "..",
  "..",
  "cabal",
);

// Intentionally malformed cabal-testsuite fixtures.
const DENYLIST = new Set([
  "cabal-testsuite/PackageTests/ProjectConfig/FieldStanzaConfusion/cabal.project",
]);

// Extensions that match our regex but aren't cabal project files.
const SKIP_EXTENSIONS = new Set([".hs", ".out", ".lock"]);

const NAME_RE = /(?:^|\/)cabal\.project(?:\.local|\.[\w.-]+)?$|\.project$/;

function* walk(dir) {
  for (const e of readdirSync(dir, { withFileTypes: true })) {
    const p = join(dir, e.name);
    if (e.isSymbolicLink()) continue;
    if (e.isDirectory()) {
      yield* walk(p);
    } else if (NAME_RE.test(p)) {
      const ext = p.slice(p.lastIndexOf("."));
      if (!SKIP_EXTENSIONS.has(ext)) yield p;
    }
  }
}

function findError(node) {
  if (node.type === "ERROR" || node.isMissing) return node;
  for (let i = 0; i < node.namedChildCount; i++) {
    const err = findError(node.namedChild(i));
    if (err) return err;
  }
  return null;
}

const parser = new Parser();
parser.setLanguage(language);

for (const f of walk(cabalDir)) {
  const rel = relative(cabalDir, f);
  if (DENYLIST.has(rel)) continue;
  test(rel, () => {
    const tree = parser.parse(readFileSync(f, "utf8"));
    if (!tree.rootNode.hasError) return;
    const err = findError(tree.rootNode);
    assert.fail(
      `ERROR node at ${err.startPosition.row + 1}:${err.startPosition.column + 1}`,
    );
  });
}

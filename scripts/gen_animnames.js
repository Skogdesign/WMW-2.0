// Generates AnimationData.csv (ID;Name) from an index-based ANIM_NAMES table
// (a current list of WoW animation names, keyed by animation index).
// Usage: node gen_animnames.js <AnimMapper.js> <out.csv>
const fs = require('fs');

const srcPath = process.argv[2];
const outPath = process.argv[3];

const src = fs.readFileSync(srcPath, 'utf8');
const m = src.match(/const ANIM_NAMES = (\[[\s\S]*?\]);/);
if (!m) { console.error('ANIM_NAMES array not found'); process.exit(1); }

// The literal is a plain array of double-quoted strings -> safe to eval.
const ANIM_NAMES = eval(m[1]);

let rows = 0;
let out = 'ID;Name\n';
ANIM_NAMES.forEach((name, i) => {
  if (typeof name === 'string' && name.length > 0) {
    out += i + ';' + name + '\n';
    rows++;
  }
});

fs.writeFileSync(outPath, out);
console.log('wrote ' + rows + ' animation names (max id ' + (ANIM_NAMES.length - 1) + ') to ' + outPath);

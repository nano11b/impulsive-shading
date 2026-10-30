import { readFile, writeFile } from "node:fs/promises";
const source = await readFile(new URL("../benchmark-results/worker/index.js", import.meta.url), "utf8");
const match = source.match(/^const page = `([\s\S]*?)`;$/m);
if (!match) throw new Error("Could not extract dashboard HTML");
const html = match[1]
  .replace('</style>', 'nav{display:flex;align-items:center;gap:20px;font-size:13px;color:#bac7dd}nav a{text-decoration:none;color:inherit}nav a:hover{text-decoration:underline}</style>')
  .replace('<div class="eyebrow">Performance lab</div></header>', '<nav aria-label="Main navigation"><a href="/" aria-current="page">Results</a><a href="/download">Download</a></nav></header>')
  .replace('Frame times include rendering, GPU completion, and buffer swap.</div>', 'Frame times include rendering, GPU completion, and buffer swap. Community results are self-reported and are not independently verified.</div>');
await writeFile(new URL("./index.html", import.meta.url), html, "utf8");
console.log("Exported dashboard HTML");

// Dumps the object definitions of the original JS game (sf.data.objects) into JSON.
// usage: node dump_defs.mjs <path-to-SuperFighters-JS> <output.json>
import fs from "fs";
import os from "os";
import path from "path";
import { createRequire } from "module";
import { pathToFileURL } from "url";

const root = path.resolve(process.argv[2]);
const outFile = process.argv[3];

// --- stubs for the browser environment -----------------------------------
globalThis.window = globalThis;
globalThis.localStorage = { getItem() { return null; }, setItem() {} };

class Img {
	constructor() { this.width = 0; this.height = 0; }
	addEventListener() {}
	set src(v) { this._src = v; }
	get src() { return this._src; }
}
class Aud {
	constructor(src) { this.src = src; this.volume = 1; this.loop = false; }
	cloneNode() { return this; }
}
globalThis.Image = Img;
globalThis.Audio = Aud;

const ctxStub = new Proxy({}, { get: () => () => {}, set: () => true });
globalThis.document = {
	cookie: "",
	getElementById(id) {
		if (id === "sf-canvas")
			return { getContext: () => ctxStub, addEventListener() {}, width: 0, height: 0 };
		return { style: {} };
	}
};

const require = createRequire(import.meta.url);
globalThis.Matter = require(path.join(root, "api/matter.js"));

// --- copy js/ to a temp dir so it can be loaded as ES modules ---------------
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "sfdump-"));
fs.cpSync(path.join(root, "js"), path.join(tmp, "js"), { recursive: true });
fs.writeFileSync(path.join(tmp, "package.json"), '{"type":"module"}');

const load = (rel) => import(pathToFileURL(path.join(tmp, rel)).href);

const sf = (await load("js/sf.js")).default;
for (const f of [
	"base_object", "player", "projectile", "fire", "effect", "gun", "melee",
	"throwable", "marker", "ladder", "platform", "background"
])
	await load(`js/Game/Objects/${f}.js`);

// --- identity maps -----------------------------------------------------------
const objects = sf.data.objects;
const keyOfObj = new Map(Object.entries(objects).map(([k, v]) => [v, k]));
const groupOf = new Map(Object.entries(sf.collision.groups).map(([k, v]) => [v, k]));
const materialOf = new Map(Object.entries(sf.materials).map(([k, v]) => [v, k]));

function conv(v, keyName) {
	if (v === null || v === undefined) return v;
	if (typeof v === "function") return { fn: true, name: v.name || keyName };
	if (v instanceof Img) return { image: v._src };
	if (v instanceof Aud) return { sound: v.src, volume: v.volume, loop: v.loop };
	if (typeof v === "object") {
		if (keyOfObj.has(v) && keyName !== "__self") return { ref: keyOfObj.get(v) };
		if (Array.isArray(v)) return v.map((x) => conv(x, keyName));
		const o = {};
		for (const [k, x] of Object.entries(v)) {
			if (k === "type") { o.cls = x.name; continue; }
			if (k === "group") { o.group = groupOf.get(x) ?? null; continue; }
			if (k === "editor") continue;
			o[k] = conv(x, k);
		}
		return o;
	}
	return v;
}

const out = { objects: {}, order: [], groups: {}, materials: {}, apparel: {} };

for (const [key, def] of Object.entries(objects)) {
	const d = {};
	for (const [k, x] of Object.entries(def)) {
		if (k === "type") { d.cls = x.name; continue; }
		if (k === "group") { d.group = groupOf.get(x) ?? null; continue; }
		if (k === "material") { d.material = materialOf.get(x) ?? null; continue; }
		if (k === "editor") continue;
		d[k] = conv(x, k);
	}
	out.objects[key] = d;
	out.order.push(key);
}

for (const [name, g] of Object.entries(sf.collision.groups))
	out.groups[name] = { category: g.category, mask: g.mask, above: g.above ?? 0 };
for (const [name, m] of Object.entries(sf.materials))
	out.materials[name] = m;
for (const [slot, items] of Object.entries(sf.data.apparel)) {
	out.apparel[slot] = {};
	for (const [name, img] of Object.entries(items)) out.apparel[slot][name] = img._src;
}

fs.writeFileSync(outFile, JSON.stringify(out, null, 1));
console.log(`dumped ${out.order.length} object definitions -> ${outFile}`);
fs.rmSync(tmp, { recursive: true, force: true });

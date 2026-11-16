/* MochiBerry --superset=typescript : core eraser tests.
   Every section prints a marker; the runner asserts exact output. */

// ── 1. plain JS must pass through byte-identical ──────────────────────
const plain = (a: number, b?: number, c = 3): number => a + (b ?? 0) + c;
console.log("T1", plain(1), plain(1, 2), plain(1, 2, 5));

// ── 2. interfaces + type aliases + class fields ───────────────────────
interface Point { x: number; y: number; label?: string }
type Vec3 = [number, number, number];
interface Animal { name: string; legs: number }
interface Dog extends Animal { bark(): string }
const p: Point = { x: 1, y: 2 };
const v: Vec3 = [1, 2, 3];
console.log("T2", p.x + p.y, v[2], p.label);

// ── 3. class with modifiers + parameter properties + generics ─────────
class Box<T> {
  private items: T[] = [];
  public readonly cap: number;
  constructor(readonly tag: string, cap: number = 8) { this.cap = cap; }
  push(item: T): void { this.items.push(item); }
  get size(): number { return this.items.length; }
  all(): T[] { return this.items; }
}
const box = new Box<string>("food", 4);
box.push("apple"); box.push("berry");
console.log("T3", box.tag, box.cap, box.size, box.all().join(","));

// ── 4. generics on arrow fns, call args, new ──────────────────────────
const ident = <T,>(x: T): T => x;
const pick = <T>(arr: T[], i: number): T | undefined => arr[i];
const N = new Box<number>();
N.push(9);
console.log("T4", ident("x"), pick([1, 2, 3], 1), N.size);

// ── 5. as / as const / satisfies / non-null / definite assignment ─────
const s = "hello" as string;
const arr = [1, 2, 3] as const;
interface Shape { kind: string }
const sq: Shape = { kind: "square" } satisfies Shape;
const maybe: number | undefined = 7;
const forced = maybe!;
let late: number;
late = 5;
console.log("T5", s, arr.length, arr[0], sq.kind, forced, late);

// ── 6. enum + const enum ──────────────────────────────────────────────
enum Color { Red, Green = 10, Blue }
const enum Dir { Up = 1, Down = -1 }
const c: Color = Color.Green;
console.log("T6", c, Color[10], Color.Blue, Dir.Up + Dir.Down);

// ── 7. namespace ──────────────────────────────────────────────────────
namespace Utils {
  export const K = 41;
  export function plus(a: number, b: number): number { return a + b; }
  export class Ctor {
    readonly v: number;
    constructor(v: number) { this.v = v; }
  }
}
console.log("T7", Utils.K, Utils.plus(1, 2), new Utils.Ctor(9).v);

// ── 8. import type / export type / inline type ────────────────────────
import type { Point as P } from "./nothing";
import { type Vec3, box } from "./nothing";
console.log("T8", "import-types-erased");

// ── 9. declare (ambient) ──────────────────────────────────────────────
declare const ambient: string;
declare function ambientFn(n: number): number;
declare class Ambient { field: number }
console.log("T9", "declares-erased");

// ── 10. optional params/props, default values, rest ───────────────────
function cfg(opts: { a: number; b?: number } = { a: 1 }, ...rest: number[]): number {
  return opts.a + (opts.b ?? 0) + rest.reduce((x, y) => x + y, 0);
}
console.log("T10", cfg({ a: 1, b: 2 }, 3, 4), cfg({}));

// ── 11. interface with method + tuple + union + literal types ─────────
interface Fn<T, R> { (x: T): R; len: number }
type Id = "a" | "b" | 0;
const id: Id = "a";
const f: Fn<number, number> = Object.assign(((x: number) => x * 2) as Fn<number, number>, { len: 1 });
console.log("T11", id, f(21), f.len);

// ── 12. index signatures + keyof + typeof type ────────────────────────
type Dict = Record<string, number>;
const d: Dict = { a: 1 };
const k: keyof Dict = "a";
type P2 = Point;
const p2: P2 = { x: 0, y: 0 };
console.log("T12", d.a, k, p2.x);

// ── 13. abstract + implements + readonly array ───────────────────────
abstract class Base {
  abstract area(): number;
  protected w: number = 0;
}
class Rect extends Base implements Shape {
  kind: string = "rect";
  constructor(readonly h: number) { super(); this.w = 5; }
  area(): number { return this.w * this.h; }
}
const r = new Rect(4);
console.log("T13", r.area(), r.kind, r.h);

// ── 14. template literals with TS inside + comment survival ───────────
const msg = `pre ${ident("inner")} post // not a comment : inside tpl`;
/* block comment with : and as and <T> inside */
console.log("T14", msg);

// ── 15. ternary, object literal with ':' (must NOT be stripped) ───────
const obj = { a: 1, b: { c: 2 }, s: "x:y", k: cond3 ? 1 : 2 };
const cond3 = true;
console.log("T15", obj.a, obj.b.c, obj.s, obj.k, cond3 ? "yes" : "no");

console.log("ALL_TS_CORE_DONE");

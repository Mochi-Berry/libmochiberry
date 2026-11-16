/* mbjs-superset.h - MochiBerry ECMAScript superset selector.
 *
 * A "superset" is a language for which every valid JavaScript (ES5/ES6) is
 * also valid, so the engine accepts it as input. Chosen from the CLI:
 *
 *   mbjs  --superset=<name> [script]
 *   mbjsc --superset=<name> [options] source
 *
 * Passing no --superset (or "default") selects the plain MochiBerry engine.
 *
 * Strict ECMAScript supersets (JS-valid == superset-valid):
 *   typescript   typed superset; optional type annotations, classes,
 *                interfaces, modules (MBTS front-end erases the types)
 *   objective-j  traditional inheritance + Smalltalk/Obj-C dynamic dispatch
 *                with optional pseudo-static typing
 *   js++         superset with a sound type system
 *   atscript     (defunct) superset of TypeScript, hence of JS
 *   six          enables ECMAScript 6 syntax via a transpiler
 *   latte        brings CoffeeScript features into JS syntax
 *   jsx          XML-like element syntax (React); JS is valid JSX
 *   oj           Objective-C-inspired superset with a small runtime
 *   yapl         superset of ECMA-262 adding Ruby/Python features
 *   civet        superset of TypeScript, hence of JS, terser syntax
 *   yappacino    esolang superset transpiled to JavaScript
 *
 * NOT supersets (kept out of the registry; the engine documents them instead):
 *   flow / hegel       type checkers, not languages
 *   coffeescript / livescript   compiled to JS but NOT strict supersets
 *   dart, elm, clojurescript, purescript, scalajs, haxe, ...  other languages
 *   mona                early codename for JavaScript itself, not a language
 *   commonjs / es6 / es2015     module systems / spec versions, not supersets
 *
 * NOTE: Takegawa (TGPM) is the MochiBerry package manager and is NOT a
 * superset; it is a separate tool/subrepo that lives alongside the engine.
 */
#ifndef MBJS_SUPERSET_H
#define MBJS_SUPERSET_H

#ifdef __cplusplus
extern "C" {
#endif

/* Name of the default engine (no superset). */
#define MBJS_SUPERSET_DEFAULT "default"

typedef struct MBJSSupersetDesc {
    const char *name;          /* selector name, e.g. "typescript" */
    const char *description;   /* one-line human description       */
    const char *status;        /* "strict" superset, or note       */
} MBJSSupersetDesc;

/* NULL-terminated registry (entry->name == NULL marks the end). */
const MBJSSupersetDesc *mbjs_superset_list(void);

/* Look up a superset by selector name; NULL for NULL/""/"default"/unknown. */
const MBJSSupersetDesc *mbjs_superset_lookup(const char *name);

/* Returns 1 when `name` selects the given superset name (exact match).
 * `name` == NULL/""/"default" are treated as the default engine. */
int mbjs_superset_is(const char *name, const char *which);

#ifdef __cplusplus
}
#endif

#endif /* MBJS_SUPERSET_H */
/* ------------------------------------------------------------------
 * Promise.allKeyed / Promise.allSettledKeyed  (ES2026 await-dictionary)
 * Faithful mirror of the PerformPromiseAllKeyed algorithm from the
 * spec, implemented in JS against the engine's native Promise.
 *
 * Key differences vs Promise.all:
 *  - input is a dictionary (any object), own enumerable string+symbol
 *    keys are awaited; non-enumerable keys are skipped.
 *  - result is a NULL-prototype object whose own keys follow the
 *    input's own-property-key order, NOT settlement order.
 *  - resolve/reject element functions are arrows (name "", no
 *    "prototype", length 1) to match builtin function shape.
 * Installed only if not already present (native engine wins).
 * ------------------------------------------------------------------ */
try { (function() {
  if (typeof Promise !== "function") return;
  if (typeof Promise.allKeyed === "function" &&
      typeof Promise.allSettledKeyed === "function") return;

  function NewPromiseCapability(C) {
    var resolve, reject;
    var promise = new C(function (res, rej) { resolve = res; reject = rej; });
    /* NewPromiseCapability: if the executor did not install callables, the
     * capability is malformed and the spec throws a TypeError. */
    if (typeof resolve !== "function" || typeof reject !== "function") {
      throw new TypeError("promise capability executor is not callable");
    }
    return { promise: promise, resolve: resolve, reject: reject };
  }

  function GetPromiseResolve(C) {
    var resolve = C.resolve;
    if (typeof resolve !== "function") {
      throw new TypeError("Promise.resolve is not callable");
    }
    return resolve;
  }

  function CreateKeyedResult(keys, values) {
    var obj = Object.create(null);
    for (var i = 0; i < keys.length; i++) {
      Object.defineProperty(obj, keys[i], {
        value: values[i],
        writable: true,
        enumerable: true,
        configurable: true
      });
    }
    return obj;
  }

  /* variant: "all" | "allSettled" */
  function PerformPromiseAllKeyed(variant, promises, C, resolve, cap) {
    var allKeys = Reflect.ownKeys(promises);
    var keys = [];
    var values = [];
    var remaining = { value: 1 };
    var i = 0;
    for (var k = 0; k < allKeys.length; k++) {
      var key = allKeys[k];
      var desc = Object.getOwnPropertyDescriptor(promises, key);
      if (desc !== undefined && desc.enumerable) {
        keys.push(key);
        values.push(undefined);
        remaining.value += 1; /* one pending element */
        let idx = i;
        i++;
        var nextValue = promises[key];
        var nextPromise = resolve.call(C, nextValue);
        /* `idx`, `alreadyCalled`, `settle` MUST be `let`: each iteration owns
         * its bindings. With `var` they'd hoist to the function scope and every
         * arrow would share the LAST iteration's settle/alreadyCalled, so only
         * one fulfillment would count and the combined promise would hang. */
        let alreadyCalled = false;
        let settle = function (fulfilled, valueOrReason) {
          if (alreadyCalled) return;
          alreadyCalled = true;
          if (variant === "all") {
            if (fulfilled) {
              values[idx] = valueOrReason;
            } else {
              cap.reject(valueOrReason);
              return;
            }
          } else {
            values[idx] = fulfilled
              ? { status: "fulfilled", value: valueOrReason }
              : { status: "rejected", reason: valueOrReason };
          }
          remaining.value -= 1;
          if (remaining.value === 0) {
            var result = CreateKeyedResult(keys, values);
            cap.resolve(result);
          }
        };
        /* onFulfilled / onRejected MUST be inline arrows so their inferred
         * `name` is "" and they have no "prototype" own property (matches the
         * builtin CreateBuiltinFunction shape the tests assert). */
        nextPromise.then(
          (value) => settle(true, value),
          variant === "all" ? cap.reject : (reason) => settle(false, reason)
        );
      }
    }
    remaining.value -= 1;
    if (remaining.value === 0) {
      var result = CreateKeyedResult(keys, values);
      cap.resolve(result);
    }
  }

  function run(variant, obj) {
    var C = this;
    var cap = NewPromiseCapability(C); /* sync TypeError if C not ctor */
    if (obj === null || (typeof obj !== "object" && typeof obj !== "function")) {
      cap.reject(new TypeError("input must be an object"));
      return cap.promise;
    }
    var resolve;
    try {
      resolve = GetPromiseResolve(C);
    } catch (e) {
      cap.reject(e);
      return cap.promise;
    }
    try {
      PerformPromiseAllKeyed(variant, obj, C, resolve, cap);
    } catch (e) {
      cap.reject(e);
    }
    return cap.promise;
  }

  /* Non-constructible, this-forwarding callable that still reports the right
   * `name`/`length`:
   *  - target is an ARROW function -> it has no [[Construct]], so the Proxy
   *    has no [[Construct]] either: isConstructor()==false and `new` throws
   *    TypeError (not-a-constructor.js).
   *  - an `apply` trap forwards the call-site `this` (C) into run(); the arrow
   *    body is never executed, so its own lexical `this` is irrelevant.
   *  - `get` / `getOwnPropertyDescriptor` traps report `name` (an arrow would
   *    otherwise report ""), everything else forwards to the target. */
  function makeKeyedFn(variant, name) {
    var target = (obj) => { throw new TypeError("unreachable"); };
    return new Proxy(target, {
      apply: function (tgt, thisArg, args) {
        return run.call(thisArg, variant, args[0]);
      },
      get: function (tgt, prop, recv) {
        if (prop === "name") {
          /* honour delete: isConfigurable() deletes the property, so the trap
           * must reflect the target's actual state or hasOwnProperty stays true */
          return Reflect.has(tgt, prop) ? name : undefined;
        }
        return Reflect.get(tgt, prop, recv);
      },
      getOwnPropertyDescriptor: function (tgt, prop) {
        if (prop === "name") {
          return Reflect.getOwnPropertyDescriptor(tgt, prop)
            ? { value: name, writable: false, enumerable: false, configurable: true }
            : undefined;
        }
        return Reflect.getOwnPropertyDescriptor(tgt, prop);
      }
    });
  }
  var allKeyed = makeKeyedFn("all", "allKeyed");
  var allSettledKeyed = makeKeyedFn("allSettled", "allSettledKeyed");

  Object.defineProperty(Promise, "allKeyed", {
    value: allKeyed,
    writable: true,
    enumerable: false,
    configurable: true
  });
  Object.defineProperty(Promise, "allSettledKeyed", {
    value: allSettledKeyed,
    writable: true,
    enumerable: false,
    configurable: true
  });

  globalThis.__mbjs_prelude.awaitdict = {
    allKeyed: typeof Promise.allKeyed === "function",
    allSettledKeyed: typeof Promise.allSettledKeyed === "function"
  };
})(); } catch (e) {
  if (globalThis.__mbjs_prelude) {
    globalThis.__mbjs_prelude.failed.push("patch_awaitdict: " + (e && e.message));
  }
}

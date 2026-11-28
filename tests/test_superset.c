/*
 * test_superset.c — tests for the MochiBerry ECMAScript superset registry.
 */

#include <stdio.h>
#include <string.h>

#include "mbjs-superset.h"

static int g_failures = 0;

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (cond) {                                                          \
            printf("PASS: %s\n", msg);                                       \
        } else {                                                             \
            printf("FAIL: %s\n", msg);                                       \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

int main(void)
{
    const MBJSSupersetDesc *d, *list;

    CHECK(mbjs_superset_lookup("typescript") != NULL, "lookup typescript");
    CHECK(mbjs_superset_lookup("typescript")->description != NULL,
          "typescript has description");
    CHECK(mbjs_superset_lookup("jsx") != NULL, "lookup jsx");
    CHECK(mbjs_superset_lookup("objective-j") != NULL, "lookup objective-j");
    CHECK(mbjs_superset_lookup("civet") != NULL, "lookup civet");

    CHECK(mbjs_superset_lookup("default") == NULL, "default is not a named superset");
    CHECK(mbjs_superset_lookup("") == NULL, "empty name is not a superset");
    CHECK(mbjs_superset_lookup(NULL) == NULL, "NULL name is not a superset");
    CHECK(mbjs_superset_lookup("nope") == NULL, "unknown name is not a superset");

    /* These must NOT be supersets: package manager, compile-to-JS languages,
     * type checkers, module systems, spec versions, and the "Mona" codename. */
    CHECK(mbjs_superset_lookup("takegawa") == NULL, "takegawa is not a superset");
    CHECK(mbjs_superset_lookup("coffeescript") == NULL, "coffeescript is not a superset");
    CHECK(mbjs_superset_lookup("livescript") == NULL, "livescript is not a superset");
    CHECK(mbjs_superset_lookup("mona") == NULL, "mona is not a superset");
    CHECK(mbjs_superset_lookup("commonjs") == NULL, "commonjs is not a superset");
    CHECK(mbjs_superset_lookup("flow") == NULL, "flow is not a superset");
    CHECK(mbjs_superset_lookup("hegel") == NULL, "hegel is not a superset");

    list = mbjs_superset_list();
    CHECK(list && list[0].name != NULL, "list is non-empty");
    CHECK(list && list[0].status && !strcmp(list[0].status, "strict"),
          "list entries are strict supersets");

    CHECK(mbjs_superset_is("typescript", "typescript"), "is: typescript==typescript");
    CHECK(mbjs_superset_is("default", "default"), "is: default==default");
    CHECK(mbjs_superset_is(NULL, "default"), "is: NULL==default");
    CHECK(mbjs_superset_is("", "default"), "is: empty==default");
    CHECK(!mbjs_superset_is("typescript", "default"), "is: typescript!=default");
    CHECK(!mbjs_superset_is("default", "typescript"), "is: default!=typescript");

    if (g_failures == 0) {
        printf("== ALL SUPERSET TESTS PASSED ==\n");
        return 0;
    }
    printf("== %d SUPERSET TESTS FAILED ==\n", g_failures);
    return 1;
}
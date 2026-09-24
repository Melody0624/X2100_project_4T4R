#include "ddma_resolver.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    struct ddma_result r;
    for (unsigned int a = 0; a < 8; ++a) {
        float b[8] = {0};
        for (unsigned int tx = 0; tx < 4; ++tx) b[(a + tx) % 8] = 10.0f - tx;
        ddma_resolve(b, &ddma_default_policy, &r);
        assert(r.status == DDMA_RESOLVED && r.anchor == a && r.candidates == (1u << a));
    }
    float collision[8] = {1,2,2,2,1,0,0,0};
    ddma_resolve(collision, &ddma_default_policy, &r);
    assert(r.status == DDMA_AMBIGUOUS && r.candidates == 3);
    float contaminated[8] = {10,10,10,10,0,6,0,0};
    ddma_resolve(contaminated, &ddma_default_policy, &r);
    assert(r.status == DDMA_CONTAMINATED);
    float weak[8] = {10,10,10,0.001f,0,0,0,0};
    ddma_resolve(weak, &ddma_default_policy, &r); assert(r.status == DDMA_WEAK);
    float empty[8] = {0};
    ddma_resolve(empty, &ddma_default_policy, &r); assert(r.status == DDMA_EMPTY);
    empty[3] = NAN;
    ddma_resolve(empty, &ddma_default_policy, &r); assert(r.status == DDMA_INVALID);
    empty[3] = INFINITY;
    ddma_resolve(empty, &ddma_default_policy, &r); assert(r.status == DDMA_INVALID);
    empty[3] = -1;
    ddma_resolve(empty, &ddma_default_policy, &r); assert(r.status == DDMA_INVALID);
    struct ddma_policy bad = ddma_default_policy; bad.minimum_winner_ratio = 1;
    ddma_resolve(collision, &bad, &r); assert(r.status == DDMA_INVALID);
    assert(ddma_signed_bin(64,128) == -64 && ddma_signed_bin(127,128) == -1);
    assert(ddma_signed_bin(-129,128) == -1 && ddma_signed_bin(128,128) == 0);
    assert(isnan(ddma_signed_bin(NAN,128)) && isnan(ddma_signed_bin(0,0)));
    puts("RESOLVER_UNIT=PASS (8 anchors, tie, contamination, weak TX, empty, invalid, wrap)");
    return 0;
}

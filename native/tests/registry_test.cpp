#include "avs3a/registry.h"
#include "avs3a/session.h"
#include "fake_backend.h"
#include <cassert>
#include <cstdio>
#include <memory>

using namespace avs3a;

int registry_test_main() {
    printf("=== Registry tests ===\n");

    {
        Registry reg;
        auto backend = std::make_unique<FakeBackend>();
        auto session = std::make_shared<Session>(HandleKind::DECODER, std::move(backend));
        session->set_epoch(1);

        int64_t id = reg.register_session(session);
        assert(id > 0);
        assert(reg.exists(id));

        auto found = reg.find(id);
        assert(found != nullptr);
        assert(found->kind() == HandleKind::DECODER);

        auto removed = reg.remove(id);
        assert(removed != nullptr);
        assert(!reg.exists(id));
        assert(reg.find(id) == nullptr);

        printf("  register/find/remove: PASS\n");
    }

    {
        Registry reg;
        int64_t id1, id2;
        {
            auto s1 = std::make_shared<Session>(HandleKind::DECODER, std::make_unique<FakeBackend>());
            auto s2 = std::make_shared<Session>(HandleKind::PARSER, std::make_unique<FakeBackend>());
            s1->set_epoch(1);
            s2->set_epoch(1);
            id1 = reg.register_session(s1);
            id2 = reg.register_session(s2);
        }
        assert(id1 != id2);
        assert(reg.find(id1)->kind() == HandleKind::DECODER);
        assert(reg.find(id2)->kind() == HandleKind::PARSER);
        printf("  multiple sessions: PASS\n");
    }

    {
        Registry reg;
        auto removed = reg.remove(999999);
        assert(removed == nullptr);
        printf("  remove nonexistent: PASS\n");
    }

    printf("=== Registry tests: ALL PASS ===\n");
    return 0;
}

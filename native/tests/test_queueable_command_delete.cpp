// A queued fitness or playlist command is deleted through QueueableCommand.
//
// The image deletes every one of them through that base with a destructor that
// is not virtual (FitnessGoalMgr.cpp and PlaylistSortMgr.cpp, 14 deletes). The
// Xbox compiler stores the base vtable and frees the block. That is undefined,
// and since the base is abstract Apple clang compiles the delete as a trap:
// opening the fitness screen on a Mac killed the game (2026-11-14,
// EXC_BREAKPOINT in FitnessGoalMgr::HandleCmdChangeProfileOnlineID). Linux
// clang freed the block with the base's size and never ran the derived
// destructor, so the Linux gate saw nothing.
//
// Native gives the base a virtual destructor. These tests fail without it: the
// derived destructor does not run on Linux, and the process traps on a Mac.
//
// The other deletes of this kind in the native build are found with
// scripts/native/delete_nonvirtual_sweep.py.

#include "test_helpers.h"

#include "meta_ham/FitnessGoalMgr.h"

#include <type_traits>

namespace {

int gProbeDestroyed = 0;

struct ProbeCommand : public QueueableCommand {
    virtual int GetType() { return 99; }
    ~ProbeCommand() { gProbeDestroyed++; }
};

}

TEST(QueueableCommandDeleteTest, TheBaseDestructorIsVirtual) {
    EXPECT_TRUE(std::has_virtual_destructor<QueueableCommand>::value);
}

TEST(QueueableCommandDeleteTest, DeletingThroughTheBaseDestroysTheCommand) {
    gProbeDestroyed = 0;
    QueueableCommand *cmd = new ProbeCommand;
    EXPECT_EQ(cmd->GetType(), 99);
    delete cmd;
    EXPECT_EQ(gProbeDestroyed, 1);
}

// The delete the fitness screen makes: a CmdChangeProfileOnlineID, which owns
// a String, released through the queue's base pointer.
TEST(QueueableCommandDeleteTest, TheOnlineIdCommandIsReleasedThroughTheBase) {
    QueueableCommand *cmd = new CmdChangeProfileOnlineID(String("a profile name"));
    EXPECT_EQ(cmd->GetType(), 0);
    RELEASE(cmd);
    EXPECT_EQ(cmd, nullptr);
}

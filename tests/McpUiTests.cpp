#include <catch2/catch_test_macros.hpp>

#include "PaintProbe.h"
#include "ui/McpConnectionsPanel.h"
#include "ui/McpConsentPanel.h"

using namespace dew;

namespace
{

/** A Settings in a temporary directory, so a test never touches the real one.
    The same fixture SettingsTests uses, for the same reason. */
struct TempSettings
{
    TempSettings()
        : directory (juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("dew-mcp-test-" + juce::Uuid().toDashedString()))
    {
        directory.createDirectory();
    }

    ~TempSettings()
    {
        directory.deleteRecursively();
    }

    std::unique_ptr<Settings> open()
    {
        return std::make_unique<Settings> (directory);
    }

    juce::File directory;
};

} // namespace

TEST_CASE ("the consent panel asks about the client by name", "[ui][mcp][consent]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    McpConsentPanel panel { { "Claude Code", "1.2.3" } };
    panel.setSize (McpConsentPanel::preferredWidth, McpConsentPanel::preferredHeight);
    panel.setVisible (true);

    // It paints, and what it paints names the thing being allowed in. A dialog
    // that asked "allow a program?" would be a dialog nobody could answer.
    const auto image = testing::render (panel);
    REQUIRE (testing::inkCoverage (image) > 0.0f);
    REQUIRE (panel.getRequest().clientName == "Claude Code");
}

TEST_CASE ("answering runs the callback once and with what was pressed", "[ui][mcp][consent]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    struct Case
    {
        int index;
        control::Grant expected;
    };

    for (const auto& c : { Case { 0, control::Grant::none }, Case { 1, control::Grant::read },
                           Case { 2, control::Grant::readWrite } })
    {
        McpConsentPanel panel { { "Some Client", "1.0" } };

        auto answers = 0;
        auto given = control::Grant::none;

        panel.onAnswered = [&answers, &given] (control::Grant grant)
        {
            ++answers;
            given = grant;
        };

        juce::Button* buttons[] { &panel.getDenyButton(), &panel.getReadButton(),
                                  &panel.getWriteButton() };

        buttons[c.index]->onClick();

        INFO ("button " << c.index);
        REQUIRE (answers == 1);
        REQUIRE (given == c.expected);

        // Pressing again must not answer twice. The server is waiting on ONE
        // callback and a second would be a grant nobody gave.
        buttons[c.index]->onClick();
        REQUIRE (answers == 1);
    }
}

TEST_CASE ("a grant survives a relaunch, and revoking it forgets it", "[ui][mcp][consent]")
{
    TempSettings temp;

    {
        auto settings = temp.open();
        McpGrants grants { *settings };

        REQUIRE (grants.grantFor ("Claude Code") == control::Grant::none);

        grants.setGrant ("Claude Code", control::Grant::readWrite);
        grants.setGrant ("Something Else", control::Grant::read);

        REQUIRE (grants.all().size() == 2);
    }

    {
        // A second Settings over the same directory: what a relaunch is.
        auto settings = temp.open();
        McpGrants grants { *settings };

        REQUIRE (grants.grantFor ("Claude Code") == control::Grant::readWrite);
        REQUIRE (grants.grantFor ("Something Else") == control::Grant::read);

        grants.revoke ("Claude Code");

        REQUIRE (grants.grantFor ("Claude Code") == control::Grant::none);
        REQUIRE (grants.grantFor ("Something Else") == control::Grant::read);
    }

    {
        auto settings = temp.open();
        McpGrants grants { *settings };

        // Revocation is written through at once rather than at the next
        // autosave: it is a security decision, and losing it to a crash would
        // be a revocation that quietly did not happen.
        REQUIRE (grants.grantFor ("Claude Code") == control::Grant::none);
    }
}

TEST_CASE ("a stored grant is never read back as more than it was", "[ui][mcp][consent]")
{
    TempSettings temp;
    auto settings = temp.open();

    // A line this build cannot read - written by a later dew, or corrupted - is
    // dropped rather than guessed at. Anything else would be a client
    // authorised by nobody.
    settings->setMcpGrants ("Claude Code\tadmin\nBroken line with no separator\n\tno name\n");

    McpGrants grants { *settings };

    REQUIRE (grants.grantFor ("Claude Code") == control::Grant::none);
    REQUIRE (grants.all().empty());
}

TEST_CASE ("a client name that could not round-trip is not stored", "[ui][mcp][consent]")
{
    TempSettings temp;
    auto settings = temp.open();
    McpGrants grants { *settings };

    // The encoding is one line per client, name and grant separated by a tab.
    // A name carrying a tab cannot come back as itself, so it is refused rather
    // than stored mangled - which would grant something to a name nobody has.
    grants.setGrant ("Bad\tName", control::Grant::readWrite);

    REQUIRE (grants.all().empty());
    REQUIRE (grants.grantFor ("Bad\tName") == control::Grant::none);
}

TEST_CASE ("the connections panel says it is off when nothing is listening", "[ui][mcp]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    TempSettings temp;
    auto settings = temp.open();
    McpGrants grants { *settings };

    McpConnectionsPanel panel { {}, [&grants] { return &grants; }, settings.get() };
    panel.setSize (McpConnectionsPanel::preferredWidth, McpConnectionsPanel::preferredHeight);

    // Openable with no endpoint at all, because "it is not running" is what
    // somebody opening this most often wants to know.
    REQUIRE (panel.getAddressText().isNotEmpty());
    REQUIRE (panel.getCommandText().isEmpty());
    REQUIRE (panel.getNumGrantRows() == 0);
    REQUIRE (testing::inkCoverage (testing::render (panel)) > 0.0f);
}

TEST_CASE ("the connections panel lists what is allowed and revokes it", "[ui][mcp]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    TempSettings temp;
    auto settings = temp.open();
    McpGrants grants { *settings };

    grants.setGrant ("Claude Code", control::Grant::readWrite);
    grants.setGrant ("A Reader", control::Grant::read);

    McpConnectionsPanel panel { {}, [&grants] { return &grants; }, settings.get() };
    panel.setSize (McpConnectionsPanel::preferredWidth, McpConnectionsPanel::preferredHeight);

    REQUIRE (panel.getNumGrantRows() == 2);
    REQUIRE (panel.getGrantRowText (0).contains ("Claude Code"));

    // A grant that could only be given would be a grant nobody should give.
    panel.revokeRow (0);

    REQUIRE (panel.getNumGrantRows() == 1);
    REQUIRE (grants.grantFor ("Claude Code") == control::Grant::none);
    REQUIRE (grants.grantFor ("A Reader") == control::Grant::read);
}

TEST_CASE ("the switch is what turns the endpoint on, and it is remembered", "[ui][mcp]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    TempSettings temp;
    auto settings = temp.open();
    McpGrants grants { *settings };

    // Off by default. A DAW that opened a port because it was installed would
    // have decided something on the user's behalf.
    REQUIRE_FALSE (settings->getMcpEnabled());

    auto asked = 0;
    McpConnectionsPanel panel {
        {}, [&grants] { return &grants; }, settings.get(), [&asked] { ++asked; }
    };

    panel.getEnableButton().setToggleState (true, juce::dontSendNotification);
    panel.getEnableButton().onClick();

    REQUIRE (settings->getMcpEnabled());

    // The panel does not own the endpoint - it is a view - so it tells whoever
    // does that the switch moved.
    REQUIRE (asked == 1);
}

TEST_CASE ("the switch survives the application rebuilding what the panel was given", "[ui][mcp]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    TempSettings temp;
    auto settings = temp.open();

    // Owned the way MainComponent owns it, because the bug was in the OWNERSHIP
    // and a stack grants object cannot express it. Ticking the switch called
    // applyMcpSettings, which replaced this unique_ptr - freeing the object the
    // panel was still holding - and the refresh at the end of the click then
    // read it. dew segfaulted before the window had finished repainting.
    auto grants = std::make_unique<McpGrants> (*settings);

    grants->setGrant ("Claude Code", control::Grant::readWrite);

    McpConnectionsPanel panel { {},
                                [&grants] { return grants.get(); },
                                settings.get(),
                                [&grants, &settings]
                                {
                                    // What applyMcpSettings used to do, verbatim.
                                    grants = std::make_unique<McpGrants> (*settings);
                                } };

    panel.setSize (McpConnectionsPanel::preferredWidth, McpConnectionsPanel::preferredHeight);

    panel.getEnableButton().setToggleState (true, juce::dontSendNotification);
    panel.getEnableButton().onClick();

    // Asked again rather than remembered, so the replacement is the one read.
    REQUIRE (settings->getMcpEnabled());
    REQUIRE (panel.getNumGrantRows() == 1);
    REQUIRE (panel.getGrantRowText (0).contains ("Claude Code"));
}

TEST_CASE ("a panel given no grants at all is still openable", "[ui][mcp]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    TempSettings temp;
    auto settings = temp.open();

    // dew_shot builds it exactly this way, and an empty std::function has to
    // read as "nothing to list" rather than as a call through nothing.
    McpConnectionsPanel panel { {}, nullptr, settings.get() };
    panel.setSize (McpConnectionsPanel::preferredWidth, McpConnectionsPanel::preferredHeight);

    REQUIRE (panel.getNumGrantRows() == 0);
    REQUIRE (testing::inkCoverage (testing::render (panel)) > 0.0f);

    // Answers null every time rather than only when empty, which is the other
    // way a callback-shaped dependency goes wrong.
    McpConnectionsPanel gone { {}, [] { return nullptr; }, settings.get() };
    gone.setSize (McpConnectionsPanel::preferredWidth, McpConnectionsPanel::preferredHeight);

    REQUIRE (gone.getNumGrantRows() == 0);
}

TEST_CASE ("the panel shows a command that names the live address", "[ui][mcp]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    TempSettings temp;
    auto settings = temp.open();
    McpGrants grants { *settings };

    class Prompt : public control::McpServer::ConsentPrompt
    {
    public:
        void ask (const control::McpServer::ClientInfo&,
                  std::function<void (control::Grant)> reply) override
        {
            reply (control::Grant::none);
        }
    } prompt;

    struct Host : control::ControlHost
    {
        juce::ValueTree project() override
        {
            return {};
        }
        juce::UndoManager* undoManager() override
        {
            return nullptr;
        }
    } host;

    control::McpServer server { host, grants, prompt };
    REQUIRE (server.start (0));

    McpConnectionsPanel panel { [&server] { return &server; }, [&grants] { return &grants; },
                                settings.get() };

    // Spelled out with the port in it, because the port is not fixed - dew
    // takes the next free one when its own is busy - so "add the URL above"
    // would mean reading a number off a dialog and typing it correctly.
    REQUIRE (panel.getAddressText() == server.getUrl());
    REQUIRE (panel.getCommandText().contains (server.getUrl()));
    REQUIRE (panel.getCommandText().startsWith ("claude mcp add"));
}

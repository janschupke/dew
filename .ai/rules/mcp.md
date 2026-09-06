# The MCP endpoint

dew runs a Model Context Protocol server inside the application, so an agent can read
and change the project that is open. `src/control/` is the library, `dew_control`, and
[Why it is this way](#why-it-is-this-way), below, is the argument.

**Off by default**, and switched on under Audio > MCP. A DAW that opens a port because it
was installed has decided something on the user's behalf.

## One table, three readers

`src/control/ControlOps.h` declares every operation once: its name, whether it reads or
writes, what it takes, what it says about itself, and the code that runs it. Three things
read it and there is deliberately no fourth — the MCP layer builds each tool's JSON
schema from `args`, the permission check reads `scope`, and `dew_mcp` walks the whole
table into the website's reference. So an operation **cannot exist without being
documented, and cannot be documented differently from how it is checked**, which is the
argument `lang::schema()` already makes for the score language.

The table is assembled by explicit `append*Ops` calls in `ops()`, never by self-
registration: these are static libraries linked without `--whole-archive`, so a `static
Registrar` would be dropped and a whole domain would vanish with no error. `Hotkeys.cpp`
records the same temptation.

## The parameter address space

**Five fields reach every value in the document** — `target`, `id`, `group`, `slot`,
`param` — and none of them is invented here. `group` is a `ParamGroup`'s `jsonKey` from
`ModuleCatalog`, which is the table that already says which node a run of parameters
lives on, so a group added to an instrument becomes addressable with no edit.

`src/control/ParamAddress.cpp` states in ONE function how that lines up with
`AutomationScope`, and delegates to `automationNodeFor` for every address automation can
also express — so the "nth EFFECT under a channel by id" walk exists once and cannot come
to mean a different effect in the two places. A test asserts the round trip over every
target `availableAutomationTargets` offers.

`AutomationScope` is deliberately NOT the address space: it is the curated automatable
set, with `masterEffect` a named gap, so addressing through it would leave a sample's
fades and a soundfont's tuning unreachable.

## What holds it up

- **Every mutation goes through `ProjectEdits`**, and **one call is one undo step**,
  however many entries its batch carried. That is what the consent dialog promises.
  `control::invoke` opens that transaction — the single place a handler is entered from,
  by the protocol layer and by the test harness both — for any operation declaring
  `OpEdits::yes`. Do not open one in a handler. Twenty-four of them used to, each naming
  itself in a string literal beside the name it already had, and four write operations
  never did, which looked exactly like a forgotten call.
- **`OpEdits` is not `OpScope`.** The scope says whether a caller needs a write GRANT; the
  edits flag says whether the DOCUMENT moves. `transport_write` moves the playhead,
  `render_audio` and `export_midi` write a file, `project_command` replaces or rewinds what
  is open — all four need a write grant and none of them edits the tree. `ControlTableTests`
  holds the flag to those four by name, so a fifth cannot join them by being forgotten.
- **`OpScope` is asked by RUNNING each operation, not by reading its name.** Every read
  operation is driven against a `FakeHost` with arguments that reach something real, and
  the document and the undo depth are compared around the call. The test used to ask
  whether a name contained `_write`, which passed an operation called `channels_remove`
  declared `OpScope::read`.
- **A batch is checked before any of it is written.** Half a batch is the worst answer
  available: the caller is told it failed and the document has moved anyway.
- **A wire argument backed by a property is NAMED by that property** — `ids::muted`, not
  `"muted"` beside it. `ArgSpec::name` is a `juce::String` for exactly this, and the gate
  on parameters spelled as literals is what asks for it.
- **Nothing in `dew_control` may reach the interface.** It links `dew_io` and stops there;
  everything session-shaped arrives through `control/ControlHost.h`, which is why the
  whole table is driveable by a test owning a ValueTree and an UndoManager and nothing
  else.

## Consent, and what actually protects the user

Four things, and none of them is a password.

- The listener binds `127.0.0.1`, so nothing off the machine can reach it.
- The `Origin` header is validated. Without it the loopback bind is not the protection it
  looks like: a page the user is merely visiting could ask their own machine to drive
  their DAW, and the browser would attach the site's origin.
- A client is **named to the user and approved by them, once**, choosing read or
  read-and-write. The name is the client's own claim about itself — which is exactly why a
  person is asked rather than a check performed.
- Every change is one undo step.

A **denial is remembered as nothing**, not as a "no": storing a refusal would mean a
client the user later wants could never ask again. A stored grant this build cannot parse
reads as `none`, never as something permissive.

`McpConsentPanel` is not a `ConfirmPanel`: that one hardcodes `Role::danger` because
everything dew asked about until now was a deletion, and this asks a question with three
answers.

## Transport

Streamable HTTP on one endpoint, `/mcp`. `src/io/LocalHttpServer.cpp` is the socket and
knows nothing about MCP; `src/control/McpServer.cpp` is the protocol and knows nothing
about sockets, which is what lets each be tested without the other.

- **GET is answered 405.** dew has nothing to push — every answer is in a response body —
  and the transport says a server offering no event stream answers exactly that.
- **A notification gets 202 and no body**, never an empty object.
- **An unknown session is 404**, because that is what tells a client to initialize again.
- **A protocol version dew does not speak is 400.** A client sends the version it
  negotiated, so anything else means the two ends disagree about what the messages mean.
- **Tests always bind port 0.** `check.sh` runs ctest with four jobs on the stated
  assumption that nothing binds a fixed port; the application takes `defaultPort` and then
  the next free one.

## The message-thread hop

A request arrives on a socket thread and the document is the message thread's, so
`McpServer::Dispatcher` crosses over and waits with a timeout. This is **not** the
lock-free command queue [realtime.md](realtime.md) refuses — that is about the audio
thread, and nothing here is on it.

It is an interface rather than a call to `callAsync` because of a hard fact about this
environment: **JUCE delivers a posted message through the platform's event loop, which in
a headless test has no `NSApplication` behind it — so `callAsync` is never delivered
however hard a test pumps, while a `juce::Timer` fires anyway through the run loop and
makes the loop look alive.** A test that could not substitute the dispatcher would have
proved the transport against a hop that silently never happened.

## Generated, committed, never hand-edited

`website/src/generated/mcp-tools.json` is written by `dew_mcp schema`, held by a test in
`dew_tests` and `cmp`'d against a second process in CI — the same arrangement the score
schema and the design tokens have, and for the reasons
[website.md](website.md) gives. Regenerate it in the commit that changes the table.

`dew_mcp` links `dew_control` and nothing else of dew's, so the operation table's claim to
need no interface is proved by a tool that builds without one rather than asserted here.

## Not translated, deliberately

Tool names, summaries, argument documentation and the guide text are English and are not
catalogue keys. They are read by a model, not by a person, and they are part of a wire
contract in the way `EffectDescriptor::id` is — see [i18n.md](i18n.md). The consent and
connections panels are ordinary interface and take keys like everything else.

## Why it is this way

**A transport-agnostic table rather than tools written against `ProjectEdits` directly.**
The alternative that was considered and rejected was a separate REST surface with MCP
translating onto it: two protocol vocabularies, two error models and two test suites for
exactly one consumer, when an MCP tool schema already *is* a typed RPC contract. What was
worth taking from that idea is the other half — a declared operation table — and this repo
had already made that move twice, for `ParamSpec` and for `lang::schema()`.

**A ninth library rather than putting it in `dew_ui`.** Linking `dew_ui` would put
`Tokens.h` in reach of protocol code and drag `juce_gui_extra` onto the link line of the
docs emitter, which is the mistake `tools/CMakeLists.txt` records twice in its own
comments. `dew_control` links `dew_io` and takes everything above it as an interface, the
way `SampleProvider.h` already does one layer down.

**Batch-first, not one tool per `ProjectEdits` function.** Sixty tools would be a long list
for a model to read and chatty for the work an agent actually does; the operations take
arrays instead, so two hundred notes is one call and one undo step. The address space is
what keeps "full control" small: it is one tool for every value in the document rather than
forty setters, and a source gate forbids the alternative anyway.

**The score language is part of the surface.** `dew_lang` already has a schema,
diagnostics, harmony and voicing, and it is byte-identical across processes — so an agent
writing a section outperforms one placing notes one at a time by a wide margin. The
primitives stay for surgery.

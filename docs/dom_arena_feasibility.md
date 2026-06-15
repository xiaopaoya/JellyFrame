# DOM Arena Feasibility

Date: 2026-06-16

This note records the current decision on moving DOM nodes to arena allocation.
Render, layout and layer trees already have arena-backed build paths because
their lifetime is a single frame or document pipeline pass. DOM nodes are more
complicated: they are observable by script, movable between attached and
detached states, and mutated after parsing.

## Current Ownership Model

- `HtmlParser::parse()` returns `std::unique_ptr<Node>` for the document root.
- `Node::children` owns child nodes with `std::unique_ptr<Node>`.
- `Node::append_child()` transfers ownership into the parent.
- `Node::detach_child()` transfers ownership out of the parent.
- `Node::remove_child()` destroys the detached subtree immediately.
- JerryScript bindings keep detached nodes alive in `JerryScriptRuntime::detached_nodes_`.
- JS `createElement()` and `createTextNode()` create detached nodes first, then
  `appendChild()` can move them into the document.

This model is easy to reason about and matches mutation-heavy UI behavior well,
but each DOM node is still a small heap allocation.

## What Is Already Improved

- DOM attributes use compact sequential `AttributeList` instead of a per-node
  hash table.
- DOM dirty flags propagate upward, so clean checks are O(1) at the root.
- DOM subtree teardown and whole-subtree `textContent` replacement now use an
  explicit work list, avoiding recursive child destruction on deeply nested
  generated documents.

## Why Full DOM Arena Is Not Immediate

A naive document arena would make node destruction cheap, but would break or
complicate current mutation semantics:

- `detach_child()` currently returns ownership. Arena-owned nodes cannot be
  individually returned as normal `std::unique_ptr<Node>`.
- JS wrappers can outlive attachment. Detached nodes need a stable owner even
  after removal from the tree.
- `removeChild()` in JS returns a node that remains usable. Immediate arena
  reclamation would be incorrect.
- Event listeners, form-control state and script wrapper references all attach
  mutable state to nodes.
- Parser-created nodes and script-created nodes currently share the same type
  and ownership path, which keeps API complexity low.

## Safe Path

1. Keep the current heap-owned mutable DOM for scripting-enabled documents.
2. Add measurement before changing ownership: node count, average children,
   average attributes and detached-node count.
3. Introduce a `Document` or `DomOwner` wrapper only when it can own both the
   root and detached nodes through one lifetime boundary.
4. Consider a parser-only arena mode for static documents that do not expose
   `detach_child()` or script-created nodes. This mode must be explicit, because
   it would have weaker mutation semantics.
5. If full mutable DOM arena is still required, use a custom node handle/deleter
   rather than raw `std::unique_ptr<Node>`, and update all mutation APIs in one
   controlled step.

## Current Recommendation

Do not convert DOM nodes to arena allocation yet. The current best tradeoff is:

- keep DOM ownership simple and correct;
- keep render/layout/layer arena-backed;
- continue removing stack and per-node overhead from the DOM incrementally;
- gather real embedded measurements before introducing a larger ownership
  abstraction.

The next low-risk DOM work should be instrumentation or a `DomOwner` design
prototype, not a direct allocator replacement.

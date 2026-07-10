# Code Style and Formatting Rules

## Scope

These rules apply to every project file. Source code, tests, tools, scripts, examples, generated templates, and documentation snippets should follow the same conventions whenever the language allows it.

When editing existing code, preserve behavior first. Style cleanup should be scoped to the lines being changed unless a file is already being reformatted intentionally.

Vendored third-party code should keep its upstream formatting unless the project maintains a local patch for that vendor.

## Comments

Use block comments only.

Single-line comment:

```cpp
/* Commentary like this */
```

Multi-line comment:

```cpp
/* Commentary line one
 * Commentary line two
 */
```

Do not use line comments.

Leave a blank line between a comment and the code element that follows it.

```cpp
/* Describe the dispatch step. */

DispatchRequest();
```

Use clear English grammar. A comment should explain purpose, non-obvious constraints, invariants, data ownership, failure handling, or compatibility behavior. Avoid comments that restate the statement below them.

Prefer concise comments near the code they explain. Do not add banners, decorative separators, or redundant section comments.

Good:

```cpp
/* Keep the existing outbox entry until the WAL has been synced. */

SyncReplicationOutbox();
```

Bad:

```cpp
/* Call the function. */

SyncReplicationOutbox();
```

## Comment Placement

Put comments above the declaration, function, branch, or block they describe.

```cpp
/* Reject requests that cannot be replayed safely. */

if (!HasReplicationId(Request))
{
     return BuildErrorResponse();
}
```

Do not place comments after closing braces.

```cpp
namespace search
{
     RunQuery();
}
```

Do not place comments at the end of executable lines.

## Braces and Blocks

Use expanded braces. Put the opening brace on its own line.

```cpp
if (condition)
{
     Statement();
}

while (condition)
{
     Statement();
}
```

Do not use compact braces.

Bad:

```cpp
if (condition) { Statement(); }
```

Every conditional, loop, and function body must use braces when the language supports them.

```cpp
for (const auto &Value : Values)
{
     Process(Value);
}
```

For guard checks, chained `if` statements may stay on one line when the second `if` is the only statement.

```cpp
if (cond) if (Instance && Instance->Logs && Instance->Logs->GetDebugMode())
{
     Instance->Logs->Debug("inverted_index", "AddDocument: Checking document count.");
}
```

Use that guard form only for short guard logging or diagnostics. Do not use it for multi-step logic.

## Blank Lines

Use blank lines to separate logical steps. A comment must be followed by a blank line before the code element it describes.

```cpp
/* Parse the request body before checking collection state. */

ParseBody();

ValidateCollection();
```

Do not pack unrelated statements together. Prefer one logical action per paragraph.

## Indentation

Use spaces only. Do not use tabs.

Top-level statements start at column zero. Each nested block adds exactly five spaces.

```cpp
if (condition)
{
     DoThing();

     /* Handle the nested case. */

     if (another)
     {
          DoOtherThing();
     }
}
```

Indentation levels:

```cpp
TopLevel();

if (LevelOne)
{
     StatementAtFiveSpaces();

     if (LevelTwo)
     {
          StatementAtTenSpaces();

          if (LevelThree)
          {
               StatementAtFifteenSpaces();
          }
     }
}
```

Continuation lines should align for readability without introducing tabs. Prefer breaking complex expressions into named variables when alignment becomes hard to read.

## Functions

Write functions with a block comment above the declaration or definition, followed by a blank line.

```cpp
/* Comment goes here. */

void Function()
{
     Body();
}
```

Constructors and functions keep braces on separate lines.

```cpp
TimerManager::TimerManager()
{

}
```

Do not write function definitions on one line.

Bad:

```cpp
void Function() { Body(); }
```

Use PascalCase for function names.

```cpp
LookLikeThis();
```

Avoid mixed-style names such as `camBack()`.

Do not change core signatures such as:

```cpp
hlquery::hlquery(int argc, char** argv)
int main(int argc, char** argv)
```

## Function Bodies

Prefer early validation for invalid input, then the main path. Keep guard checks small and explicit.

```cpp
/* Reject empty input before normalizing it. */

if (Value.empty())
{
     return false;
}

NormalizeValue(Value);
```

Do not hide major behavior in deeply nested branches. Split helper functions when a function becomes difficult to scan.

## Variables

Leave a blank line above and below each variable declaration unless exactly two related variables are grouped together.

```cpp
/* Number of documents accepted by this batch. */

unsigned int accepted_count = 0;

unsigned int rejected_count = 0;

ProcessBatch();
```

Exactly two related variables may be grouped.

```cpp
std::string host;
unsigned int port = 0;

ParseEndpoint(host, port);
```

Avoid names ending with underscores. Prefer names like `vars_like_this`.

Use descriptive names that match domain concepts. Avoid abbreviations unless they are established in the project.

Good:

```cpp
std::string CollectionName;
unsigned int RetryCount = 0;
```

Bad:

```cpp
std::string cn;
unsigned int RetryCount_ = 0;
```

## Data Types

Prefer appropriate optimized types when useful, including `unsigned int`, `signed int`, `long`, and `double`.

Use unsigned types for counts, sizes, and values that cannot be negative. Use signed types when negative values are meaningful. Use `double` for scoring and ranking calculations unless a narrower type is required by an API.

Avoid implicit narrowing. Cast explicitly when crossing API boundaries.

```cpp
const unsigned int ResultCount = static_cast<unsigned int>(Results.size());
```

## Naming

Use PascalCase for function names and type names.

```cpp
BuildSearchResponse();
SearchResultSet Results;
```

Use clear variable names that are consistent with nearby code. Existing local naming conventions may be preserved when changing a narrow area.

Use all-caps only for compile-time constants and macros that already follow that convention.

## Logging

Do not use `LOG_DEBUG`.

Use project logging directly.

```cpp
Instance->Logs->Debug("test", "Dispatch completed.");
Instance->Logs->Normal("test", "Dispatch completed.");
```

Every log message must end with one period. Do not add extra punctuation after the period.

Log calls must stay on one physical line. Keep `Instance->Logs->Debug(...)` and `Instance->Logs->Normal(...)` arguments unwrapped.

Good:

```cpp
Instance->Logs->Debug("search_api", "Replication outbox committed.");
```

Bad:

```cpp
Instance->Logs->Debug("search_api",
     "Replication outbox committed.");
```

Use stable subsystem names. Avoid logging secrets, tokens, credentials, raw authorization headers, or full request bodies.

## Includes

Put system headers first with angle brackets, followed by local headers with quotes.

```cpp
#include <string>
#include <vector>

#include "core/hlquery.h"
#include "utils/tools.h"
```

Keep include groups separated by one blank line. Sort includes within each group when practical.

Do not include a header only because another header currently needs it. Each file should include what it uses directly.

## Namespaces

Do not add comments at the end of namespace closing braces.

```cpp
namespace something
{
     StartsHere();

     /* Comment */

     Value = 1;
}
```

Keep namespace bodies formatted like any other block with five-space indentation per nested level.

## Classes

Declare private members and methods first. Declare public members and methods second.

Indent `private` and `public` labels by three spaces, and leave a blank line beneath each label.

```cpp
class Example
{
   private:

     /* Internal initialization function. */

     void Initialize();

     unsigned int RetryCount = 0;

   public:

     /* Construct the object. */

     Example();
};
```

Keep class comments focused on ownership, lifecycle, and invariants. Do not list every method in prose when the declarations are already clear.

## Conditionals

Use braces and expanded formatting for all conditionals.

```cpp
if (Request.empty())
{
     return BuildErrorResponse();
}
else if (Request.IsRetry())
{
     RetryRequest();
}
else
{
     DispatchRequest();
}
```

Prefer positive conditions when they make the flow easier to read.

Do not combine unrelated checks into a single long expression. Break them into named booleans when the condition has domain meaning.

```cpp
const bool HasWritableDatabase = Instance && Instance->Database;

if (!HasWritableDatabase)
{
     return false;
}
```

## Loops

Use braces and expanded formatting for all loops.

```cpp
for (const auto &Item : Items)
{
     Process(Item);
}

while (HasPendingWork())
{
     RunNextTask();
}
```

Keep loop bodies small. Move complex validation or transformation into helper functions.

## Switch Statements

Format switch statements with braces on their own lines. Indent cases one level inside the switch block.

```cpp
switch (Action)
{
     case RouteAction::Health:
          return HandleHealth();

     case RouteAction::Search:
          return HandleSearch();

     default:
          return HandleNotFound();
}
```

Use a blank line between non-trivial cases.

## Exceptions and Error Handling

Keep exception handlers explicit. Catch the narrowest exception type that is useful.

```cpp
try
{
     ParsePayload();
}
catch (const nlohmann::json::exception &Error)
{
     return BuildErrorResponse();
}
```

Do not swallow errors silently. If an error is intentionally ignored, document the reason with a block comment.

## Return Values

Return early for invalid state. Keep success paths easy to scan.

```cpp
if (!IsReady())
{
     return false;
}

return Commit();
```

Do not return magic values without context. Use named constants, enums, or helper functions when the meaning is not obvious.

## Expressions

Prefer readable expressions over dense expressions. Split long calculations into named variables.

```cpp
const double NormalizedLength = DocumentLength / AverageLength;

const double Score = CalculateScore(TermFrequency, NormalizedLength);
```

Avoid assignments inside conditions unless the surrounding code already uses that pattern clearly.

## File Layout

Keep files organized from broad dependencies to concrete implementation.

Recommended C++ source layout:

```cpp
#include <algorithm>
#include <string>
#include <vector>

#include "core/hlquery.h"
#include "utils/tools.h"

/* File-level implementation summary. */

static bool IsValidState()
{
     return true;
}

/* Public operation implemented by this file. */

void RunOperation()
{
     ExecuteOperation();
}
```

Avoid large blocks of unrelated helpers between a public function and the private helper that exists only for that function. Keep related logic close enough that a reviewer can follow the flow without jumping around the file.

## Declarations

Place declarations near their first use when the scope is local. Prefer the smallest practical scope.

```cpp
if (ShouldLoadConfig())
{
     std::string ConfigPath = BuildConfigPath();

     LoadConfig(ConfigPath);
}
```

Avoid declaring variables at the top of a function when they are not needed until much later.

Use `const` for values that are not reassigned.

```cpp
const std::string CollectionName = ExtractCollectionName(Path);
```

Prefer references for non-owning access to existing objects. Prefer values when ownership or lifetime should be independent.

## Constants

Use named constants for repeated values, protocol limits, sizes, and sentinel values.

```cpp
static constexpr unsigned int MaxRetryCount = 3;

if (RetryCount > MaxRetryCount)
{
     return false;
}
```

Keep constants close to the subsystem that owns their meaning. Avoid global constants for behavior that is only meaningful inside one source file.

## Pointers and References

Check nullable pointers before dereferencing them.

```cpp
if (Instance && Instance->Logs && Instance->Logs->GetDebugMode())
{
     Instance->Logs->Debug("runtime", "Runtime check completed.");
}
```

Use references when a value must exist. Use pointers when absence is a valid state.

Do not store references to temporary values or objects whose lifetime is not clear.

## Memory and Ownership

Prefer RAII objects for ownership and cleanup. Avoid manual cleanup paths when a standard container or smart pointer can express ownership clearly.

```cpp
std::vector<std::string> PendingItems;
```

Use raw pointers only for non-owning relationships or APIs that require them. Document ownership transfer when it is not obvious.

## Lambdas

Use lambdas for short local behavior. Move longer logic into a named helper function.

```cpp
auto IsReady = [](const Task &Item)
{
     return Item.Enabled && !Item.Name.empty();
};
```

Use explicit captures when practical. Avoid broad captures in long functions.

## Templates and Generics

Keep template code readable. Use meaningful type names and avoid deeply nested expressions.

```cpp
template <typename ValueType>
ValueType ClampValue(ValueType Value, ValueType MinValue, ValueType MaxValue)
{
     return std::max(MinValue, std::min(Value, MaxValue));
}
```

Prefer ordinary functions when templates do not remove real duplication.

## Macros

Avoid macros for normal control flow. Prefer functions, constants, enums, and templates.

Use macros only when the surrounding subsystem already requires them, such as hook dispatch or compile-time feature flags.

Macro names should be visibly distinct and should not hide side effects.

## Enums

Prefer scoped enums for new code.

```cpp
enum class WriteState
{
     Pending,
     Committed,
     Failed
};
```

Use explicit names that make log output, switch statements, and error handling easy to understand.

## JSON and Structured Data

Build structured data with structured APIs instead of string concatenation when practical.

```cpp
nlohmann::json Response;

Response["status"] = "ok";
Response["count"] = ResultCount;

return Response.dump();
```

When string construction is required for compatibility, escape all user-controlled values before adding them to JSON or protocol output.

## Input Validation

Validate external input before using it for storage keys, paths, routing, shell commands, network targets, or file operations.

```cpp
if (CollectionName.empty())
{
     return BuildErrorResponse();
}
```

Validation should reject invalid data explicitly. Do not rely on later failures to catch malformed input.

## Security-Sensitive Code

Keep security checks obvious and close to the action they protect.

Never log secrets, tokens, passwords, API keys, raw authorization headers, private keys, or full request bodies that may contain credentials.

Use constant-time comparison helpers when comparing secrets if the project provides them.

## Concurrency

Keep lock scopes as small as practical. Do not perform blocking network or disk work while holding a mutex unless the subsystem requires it.

```cpp
{
     std::lock_guard<std::mutex> Lock(StateMutex);

     CurrentState = NextState;
}

NotifyWaiters();
```

Document lock ordering when two or more locks must be held together.

## I/O and Filesystem

Use filesystem APIs for path operations instead of manual string slicing when practical.

Validate paths before reading or writing. Reject path traversal attempts before opening files.

When writing persistent state, prefer atomic or recoverable update patterns already used by the surrounding subsystem.

## Network and Protocol Code

Normalize and validate routes before extracting IDs or dispatching handlers.

Keep protocol response construction consistent with existing helpers.

For error responses, include stable machine-readable codes when the API layer already supports them.

## Tests

Tests should focus on observable behavior and regressions. Add test cases near existing tests for the subsystem being changed.

```cpp
if (Actual != Expected)
{
     std::cerr << "expected " << Expected << ", got " << Actual << '\n';
     return EXIT_FAILURE;
}
```

Prefer deterministic fixtures. Avoid tests that depend on wall-clock timing unless timing behavior is the feature under test.

Regression tests should cover the failing shape and at least one valid neighboring shape.

## Scripts

Use the closest equivalent of these rules in scripts.

Shell scripts should quote variables, validate inputs, and fail clearly.

```sh
if [ -z "$TARGET_DIR" ]; then
     printf '%s\n' 'missing target directory'
     exit 1
fi
```

Keep generated file paths explicit. Avoid destructive commands unless the script is specifically a cleanup tool and validates its target first.

## Documentation

Documentation examples should compile or run when copied into the intended context whenever practical.

Keep examples short and focused on the concept being documented.

Code snippets in documentation should follow the same formatting rules as source files.

## Generated Files

Do not hand-edit generated files unless the generated file is the source of truth for that subsystem.

When changing generated output, update the generator or template and regenerate the output as a separate, reviewable step.

## Dependencies

Avoid adding dependencies for small helpers that the standard library or existing project utilities already cover.

When a new dependency is necessary, document the runtime, build, licensing, and packaging impact.

## Compatibility

Preserve public API behavior unless the change is intentionally breaking and documented.

When tightening validation, check existing valid routes and compatibility aliases before rejecting new shapes.

## Refactoring

Keep refactors focused. Behavior changes and broad formatting changes should be separate.

Rename only within the ownership boundary of the change unless a larger migration is intentionally planned.

When extracting helpers, make the helper name describe the reason for the extraction, not only the operation it performs.

## Performance

Prefer clear code first, then optimize measured hot paths.

When optimizing, keep the correctness condition visible. Avoid shortcuts that make result ordering, filtering, or validation depend on container iteration order unless that order is guaranteed.

Use reserve calls when the expected container size is known and the surrounding code benefits from avoiding repeated allocations.

## Error Messages

Error messages should be specific, grammatical, and actionable.

Good:

```cpp
return BuildErrorResponse(Status::BAD_REQUEST, Code::DOCUMENT_INVALID_ID, "Invalid document ID", "Document ID validation failed.");
```

Avoid vague messages that hide the failing input category.

## Formatting Existing Code

When touching existing files, apply these rules to new and modified lines. Avoid broad whitespace churn unless the task is specifically a formatting pass.

When a local area already follows a different pattern, improve only the changed block unless changing the whole file is safer and reviewed as a formatting-only change.

Do not mix behavior changes with large style-only rewrites.

## Review Checklist

Before submitting code, check the following:

- Comments use block syntax only.
- Comments are separated from following code by a blank line.
- Braces are expanded and opening braces are on their own line.
- Indentation uses spaces only.
- Each nested block adds exactly five spaces.
- Functions use PascalCase names unless preserving an existing API.
- Variables do not end with underscores.
- Logs use `Instance->Logs->Debug` or `Instance->Logs->Normal`.
- Log messages end with one period.
- Debug and normal log calls stay on one physical line.
- Includes are grouped with system headers first and local headers second.
- Namespace closing braces have no comments.
- Class sections put private declarations before public declarations.
- `private` and `public` labels are indented by three spaces and followed by a blank line.
- Core signatures such as `hlquery::hlquery(int argc, char** argv)` and `int main(int argc, char** argv)` remain unchanged.

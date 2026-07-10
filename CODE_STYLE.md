# Code Style and Formatting Rules

## Scope

These rules apply to every file in this project.

## Comments

Use block comments only.

Single-line comments:

```cpp
/* Commentary like this */
```

Multi-line comments:

```cpp
/* Commentary line one
 * Commentary line two
 */
```

Do not use `//` comments.

Leave a blank line between a comment and the code element that follows it.

```cpp
/* Comment like this */

FunctionHere();
```

Use clear English grammar. Add meaningful comments where they help explain intent, and avoid comments that restate obvious code.

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

For guard checks, chained `if` statements may stay on one line when the second `if` is the only statement.

```cpp
if (cond) if (Instance && Instance->Logs && Instance->Logs->GetDebugMode())
{
     Instance->Logs->Debug("inverted_index", "AddDocument: Checking document count.");
}
```

## Indentation

Use spaces only. Do not use tabs.

Indent by exactly five spaces for each nested block level.

```cpp
if (condition)
{
     DoThing();

     /* Another if */

     if (another)
     {
          DoOtherThing();
     }
}
```

## Functions

Write functions with a block comment above the declaration or definition, followed by a blank line.

```cpp
/* Comment goes here */

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

Do not change core signatures such as:

```cpp
hlquery::hlquery(int argc, char** argv)
int main(int argc, char** argv)
```

## Variables

Leave a blank line above and below each variable declaration, unless exactly two related variables are grouped together.

```cpp
/* Variable description */

var1 = 4;

var2 = 3;
```

Avoid names ending with underscores. Prefer names like `vars_like_this`.

## Naming

Use PascalCase for function names.

```cpp
LookLikeThis()
```

Avoid mixed-style names such as `camBack()`.

## Logging

Do not use `LOG_DEBUG`.

Use:

```cpp
Instance->Logs->Debug("test", "Dispatch completed.");
Instance->Logs->Normal("test", "Dispatch completed.");
```

Every log message must end with one period. Do not add extra punctuation after the period.

Write log calls on a single physical line. Keep `Instance->Logs->Debug(...)` and `Instance->Logs->Normal(...)` arguments unwrapped.

## Includes

Put system headers first with angle brackets, followed by local headers with quotes.

```cpp
#include <vector>

#include "hlquery.h"
```

## Namespaces

Do not add comments at the end of namespace closing braces.

```cpp
namespace something
{
     StartsHere();

     /* Comment */

     var = 1;
}
```

## Classes

Declare private members and methods first, then public members and methods.

Indent `private` and `public` labels by three spaces, and leave a blank line beneath the label.

```cpp
class Example
{
   private:

     /* Internal initialization function */

     void Initialize();

   public:

     /* Construct the object. */

     Example();
};
```

## Data Types

Prefer appropriate optimized types when useful, including `unsigned int`, `signed int`, `long`, and `double`.

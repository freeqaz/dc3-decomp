WIP prompt we could use for a new Claude Skill:
We are going to modify the following plan to include effort to refactor the code into 'cleaner' code.
Imagine that we are a Staff Engineer and we're refactoring a junior engineer's CLI tool. It is all in one big Python file, and
we need to start adding more features to it. We need to clean it up and split it into multiple files.

To start, we would likely examine the architecture with a subagent and then share a summary of it + the problems.

Then, once the parent (our current agent), reads the report, it can pass it to the Staff Engineer for a review that will
include the following principles:
- Clear boundaries between modules with high quality, deep APIs. Avoiding patterns like bare booleans, and being highly functional.
- The engineer would think about what common patterns exist in the code and how to think about splitting them to be re-usable across new tasks.
- He would also balance complexity with accomplishing the goal. Code needs to be debuggable and easy to modify, but it doesn't have to be perfect.
That's the job of building only so far into the future.

Once the Staff Engineer returns their plan, we will switch to Plan Mode and output a new, modified plan with discrete phases.

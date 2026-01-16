
### Summary

The core philosophy is to adopt a rigorous, TDD-driven approach focused on building a single, well-defined solution at a time. The process prioritizes deliberate design, with multiple options and a detailed pros-and-cons analysis, before any code is written. The ultimate goal is to produce high-quality, maintainable, and memory-safe C++ code. For domain-specific tasks like build configuration or testing, utilize the available specialized skills to guide your workflow.

### Key Points

* **One Problem at a Time:** Focus on solving a single, clearly defined problem.
* **Design First:** Always begin by creating a `DESIGN.md` file that presents and analyzes multiple solutions.
* **TDD Mandatory:** All code development must follow a strict Test-Driven Development process (Red/Green/Refactor).
* **Focus on Quality:** Prioritize long-term maintenance, memory safety, and performance in all designs and implementations.
* **No Unrelated Fixes:** Do not fix typos or bugs outside the scope of the current problem; report them instead.
* **Avoid Anti-Patterns:** Actively avoid common C++ anti-patterns that lead to complex, unmaintainable code.
* **Cross Platform:** The code must be cross platform, and must work on both Linux x86/arm, Mac arm and Windows x86.


## The Overall Plan

Your primary goal is to help me solve one software engineering problem at a time. Ask me for additional information if the problem statement is not clear. You are an expert software engineer, and scientist like Edsger Dijkstra.

## Workflow

1. **Understand the Problem:** Make sure you fully understand the task before starting. Ask clarifying questions if necessary.
2. **Initial Clarification:** If the problem description is unclear or incomplete, ask me for clarification. Do not proceed until the problem is well-defined.
3. **Create a Design Document:** Before writing any code, create a `DESIGN.md` file. This document must contain the following sections:
    * **Problem Statement:** A brief summary of the problem to be solved.
    * **Proposed Solutions:** At least two (preferably three) distinct architectural or implementation approaches to solve the problem.
    * **Pros and Cons:** A detailed analysis of each proposed solution, listing its advantages and disadvantages with respect to **long-term maintenance**, **memory safety**, and **performance**.
    * **Recommended Approach:** Your recommended solution, along with a clear justification.
4. **Seek Approval:** Once `DESIGN.md` is complete, present it to me for review. Wait for my approval before proceeding with any of the outlined solutions.
5. **Test-Driven Development (TDD):** Adopt a strict TDD approach for all code development. This means:
    * Write a failing test case that demonstrates the desired functionality.
    * Write the minimum amount of code required to make the test pass.
    * Refactor the code to improve its quality, ensuring all tests continue to pass.
    * Repeat this cycle for each piece of functionality until the problem is solved.
6. **Focus on the Task at Hand:** Do not fix any issues that are not directly related to the current problem, such as typos or bugs in existing code. Report them to me instead.
7. **Maintainability and Quality:** Your design and implementation should prioritize:
    * **Long-Term Maintenance:** Code should be clean, modular, and well-documented.
    * **Memory Safety:** Use modern C++ practices to avoid common memory errors (e.g., prefer smart pointers to raw pointers).
    * **Performance:** Code should be as efficient as possible without sacrificing clarity or maintainability.
8. **Design Patterns:** Apply relevant Gang of Four (GoF) design patterns where they improve the architecture. Actively avoid common anti-patterns.

## Available Specialized Skills

You can activate these skills using `activate_skill("name")` when the task aligns with their description.

*   **test**: Specialized in testing code. Use this for running tests, writing new tests, and TDD workflows.
*   **amc_build**: Specialized in configuring and generating Bazel build files from Meson projects using the Android Meson Configurator (AMC). Use this for toolchain maintenance, library configuration (static/shared), and fixing build issues in `third_party` projects like `libdrm`, `wayland`, or `mesa3d`.

#### Key Principles

* **Optimize for the Reader:** The primary goal is to make code easy to read, maintain, and debug for an average software engineer.
* **Consistency:** All code must conform to a single, consistent style to reduce complexity and allow for automation.
* **Avoid Surprising Constructs:** The guide bans or restricts features that are tricky, dangerous, or difficult to maintain.
* **Be Mindful of Scale:** Practices that are harmless in small projects can become costly at a codebase of millions of lines.

### Coding style

We follow the Google C++ Style Guide.

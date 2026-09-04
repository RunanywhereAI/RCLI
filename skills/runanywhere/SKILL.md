---
name: runanywhere
description: Set up and use RunAnywhere from the terminal — install rcli, sign in, pick a coding harness, run a model, check spend. Use when the user wants to get started with RunAnywhere, run a harness like opencode against a hosted or on-device model, or asks what their usage is.
---

# RunAnywhere

`rcli` is one CLI for two things: models running **on this machine**, and models
served from **RunAnywhere Cloud**. The same commands cover both — if a model is
on the machine it is served locally, otherwise the request goes to the console
the user is signed in to and is metered against their balance.

## First check what is already true

```bash
rcli whoami     # signed in? which console?
rcli backends   # which engines this build linked
```

`rcli whoami` failing with "not signed in" is the only thing that needs fixing
before anything else works against the cloud. On-device models need no account.

## Signing in

```bash
rcli login
```

Opens the console in a browser. The person signs in with Google or GitHub,
approves the terminal, and the CLI stores a key in `~/.config/rcli`. There is no
password and no organization step. If a browser cannot open, `rcli login
--no-browser` prints the URL to visit.

## Coding harnesses

A harness is an existing coding tool that `rcli` wires to a model. Today that is
**opencode**.

```bash
rcli opencode --cloud -m glm-5.3        # hosted, metered
rcli opencode -m qwen3-0.6b             # a model on this machine
```

If opencode is not installed, `rcli` says so and prints the install command
(`npm i -g opencode-ai`) rather than failing. Install it, then run the same
line again.

Offer to explain a harness before running it. Someone who has just signed up
does not yet know what opencode is, and "would you like me to explain how the
opencode harness works?" is a better second message than a launched TUI.

## Running a model directly

```bash
rcli pull qwen3-0.6b     # download it
rcli list                # what is downloaded
rcli run qwen3-0.6b      # talk to it
```

Models land in `~/.local/share/runanywhere`. Nothing is downloaded until asked.

## Spend

```bash
rcli usage               # credit left, then input/output/cache tokens and spend
rcli usage --json
```

Read-only, and scoped to the signed-in account.

## When something is wrong

- **"not signed in"** — `rcli login`.
- **"that key is not valid"** — the key was revoked or expired; `rcli login` again.
- **opencode not installed** — `npm i -g opencode-ai`.
- **a model is slow or unavailable** — `rcli backends` shows which engines this
  build actually linked; a model needing an engine that is not there will not run.

## What not to do

Do not print, log or echo the contents of `~/.config/rcli/credentials.json`.
It holds a key with the person's credit behind it.

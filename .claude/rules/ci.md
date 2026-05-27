---
paths:
  - ".github/workflows/**"
  - ".github/**"
---

# CI Workflow Rules

- Every CI gate must have a local `make` equivalent. CI never checks
  something `make check` doesn't.
- Never weaken a CI check to make a PR pass. Fix the code.
- Workflow triggers: push to master + pull_request.
- Matrix builds: Debug + ASan presets. Both must pass.
- New checks: add the workflow job AND the `make` target in the same PR.
- Secrets: use GitHub Actions secrets, never hardcode tokens.
- Timeout: set reasonable timeouts on long jobs (ASan builds).

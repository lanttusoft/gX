<!-- //ai: contact routing for public reports; no private email address. -->
# Support

gX is a public beta provided as source, without a support SLA. Use the
repository issue templates for public reports so the version, platform, and
privacy checks are included.

## Contact routing

| Topic | Where to go |
| --- | --- |
| Build/renderer/tooling help | [`docs/README.md`](docs/README.md) and the documentation contact link on the new-issue page |
| Bug report | [Bug report form](../../issues/new?template=bug_report.yml) |
| Feature proposal | [Feature request form](../../issues/new?template=feature_request.yml) |
| Suspected vulnerability | Private advisory link in `SECURITY.md`; never a public issue |
| Conduct concern | Private message to the repository owner through their GitHub profile |
| Everything else | Open a documentation or support issue using the closest template |

Do not use public issues for credentials, private paths, personal data, gameX
assets, story content, or private scenes. For a conduct concern, avoid posting
sensitive details publicly; use the owner's GitHub profile or GitHub's
report-abuse flow.

## What to include

- **Bug reports:** exact version or commit, operating system and display
  server, GPU/driver, build mode, reproduction steps, expected versus actual
  behavior, frequency, and relevant logs.
- **Feature requests:** the engine problem first, then the proposed behavior
  and the smallest useful scope.
- **Evidence:** screenshots and diagnostic JSON are optional. Inspect them for
  private scene context before attaching.

The beta is tested primarily on Linux/X11. Reports from other platforms are
useful, but support may be limited by available hardware and drivers.

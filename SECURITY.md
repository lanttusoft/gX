# Security policy

gX is a GPLv3 game engine. Security reports are welcome, but this project does
not promise a fixed response time.

## Reporting

Do **not** open a public issue for a suspected vulnerability. Use GitHub's
private vulnerability reporting for this repository when it is enabled, or
contact the maintainer privately through the repository owner account.

Include:

- affected commit/tag and platform;
- minimal reproduction steps;
- whether the issue is reachable in the default build or only in an opt-in
  tool/feature;
- logs with secrets, personal paths and private assets removed.

## Scope

The engine, build scripts, resource loaders, presets and optional system
dependencies are in scope. Third-party content and the author's private
development archive are not published here. Issues in a third-party
dependency should also be reported to that project.

## Not security boundaries

The local module/Host ABI and the Battle LAN prototype are development
interfaces, not authenticated network security boundaries. Do not expose
development modules, tool servers or unauthenticated game sessions to
untrusted networks.

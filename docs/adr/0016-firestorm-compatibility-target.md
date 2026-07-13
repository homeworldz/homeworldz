# ADR 0016: Firestorm Compatibility Target

Status: Accepted

The first playable HomeWorldz slice targets the 64-bit, OpenSim-enabled
Firestorm 7.2.4 release branch at source commit
`10bd3c9f930c76e1427ddd4ecece6cdf36b4406d`. The upstream viewer version at
that commit is 26.1.1.

The source commit is the stable protocol reference. Manual smoke-test records
also capture the installed binary's full About version and checksum. Viewer pin
upgrades are explicit changes that must rerun the login and region-entry smoke
test described in `../FIRESTORM.md`.

HomeWorldz implements Firestorm's required external XML-RPC/LLSD and UDP edge
protocols without adopting them for communication between HomeWorldz services.

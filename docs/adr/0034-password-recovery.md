# ADR 0034: Self-Service Password Recovery

Status: Accepted (decisions made 2026-08-05; implementation pending)

There is no recovery flow. `accountPassword` requires the current password and an
authenticated session, so it serves someone changing a password, not someone
locked out. `set-password` covers the operator console case only. The gap is fine
while the operator is the only account holder and becomes a support problem the
week a second person joins.

Four decisions were taken before implementation so the token design has something
to satisfy.

## A reset does not end existing sessions

The common default is to invalidate every session on reset, on the theory that a
reset signals compromise. **Here it does not**, and the reasoning is worth keeping
because it is a deliberate departure.

In this product a reset overwhelmingly means the password was forgotten or a saved
credential was lost, not that an account was taken. The operator hit exactly that
case on 2026-08-05 by deleting Firestorm's stored credentials: the password was
gone from where it was kept, the account was never at risk, and a live viewer
session had every right to continue. Ending sessions would have punished the
ordinary case to guard against a rarer one.

This is a judgement about *this* product rather than a general security position.
If the threat model changes — paid balances, higher-value accounts, evidence of
account theft — revisit it, and expect the answer to flip.

## The contract is written before the code

`api/openapi.yaml` is the only artefact both the website and the grid read, so it
is updated first and the implementation follows it. Writing it first is what stops
the two ends diverging, and this repository spent 2026-08-05 correcting four
documents that had drifted from behaviour — an agreed contract is cheaper than a
reconciliation.

## Rate limiting and lockout are in scope from the start

A public endpoint that sends mail on request is an abuse vector even when its
reply reveals nothing. Throttling is part of the first implementation rather than
a hardening pass, because "add limits later" leaves a working amplifier in the
meantime.

## The reset link goes only to the address on file

Not negotiable. The request endpoint also answers identically whether or not an
address is known, so it cannot be used to enumerate accounts.

## Shape of the work

A single-use reset token with a short expiry, compared in constant time; a public
request endpoint; a consume-and-set endpoint; the mail send, modelled on the
existing two-step registration flow and the Cloudflare mail path (`[mail]`
`verification_url` in `grid.ini` suggests a sibling `reset_url`); and two screens
plus a link on the login page, which currently offers only registration
(`web/src/pages/LoginPage.jsx:80`).

Account recovery is the mechanism by which an account is taken over, so it is
worth more care than its endpoint count suggests. The happy path is small; the
failure paths are the feature.

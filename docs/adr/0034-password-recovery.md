# ADR 0034: Self-Service Password Recovery

Status: Accepted (decisions made 2026-08-05; implementation pending)

There is no recovery flow. `accountPassword` requires the current password and an
authenticated session, so it serves someone changing a password, not someone
locked out. `set-password` covers the operator console case only. The gap is fine
while the operator is the only account holder and becomes a support problem the
week a second person joins.

Four decisions were taken before implementation so the token design has something
to satisfy.

## A reset is a password change with a different proof of identity

This is the framing the rest follows from (operator, 2026-08-05). A reset and a
user-initiated change are **the same operation**. They differ only in how the
requester is shown to be the account holder:

| | proof of identity |
| --- | --- |
| change | the current password, plus an authenticated session |
| reset | a single-use token sent to the address on file |

After that proof, both do exactly one thing: write the password hashes so the new
password works. Nothing else — no session changes, no side effects, no extra
state. **All the security work is in the proof**, which is why the token design
gets the care and the password-setting does not need any beyond writing both
digests.

That primitive already exists. `identity.SetPassword` was added on 2026-08-05 for
the `set-password` console tool: it writes the bcrypt and viewer MD5 digests for a
named account and touches nothing else. Both flows should sit on it rather than
growing their own write paths — an account that can sign in to the website but not
a viewer is worse than one that cannot sign in at all, and one write path is how
that stays impossible.

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

This also follows from the framing above: a user changing their own password does
not expect to be logged out of anything, and a reset is the same operation.

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

## The reset URL defaults to the management origin, in the code as well as the file

`reset_url` must default to `https://my.homeworldz.com/reset` in `config.go`, not
only in a deployed `grid.ini`. The website session raised this and the precedent is
theirs (2026-08-05, verified here).

`verification_url` was once repointed on the live grid while `config.go` still
defaulted to `https://homeworldz.com/verify`. A fresh deployment without the
setting would have fallen back to the website origin and **kept working**, because
`public/_redirects` on homeworldz.com 302s `/verify` to the management site. A
silent dependency on a redirect in another repository, invisible because nothing
failed. Fixed in `baadf82`; the default is now the `my.` origin.

So the reset default starts there. The website can add a `/reset` redirect rule on
request, but that is a safety net for mail already in flight — never the intended
path, and not a substitute for the default being right.

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

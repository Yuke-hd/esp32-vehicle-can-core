## Summary

<!-- What changed and why? Keep this focused on observable behavior and design intent. -->

## Related issue

<!-- Use Closes #123 only when this PR fully satisfies the issue. -->

Refs #

## Scope

### Included

-

### Not included

- Make/model decoders, vehicle services, and product firmware; maintain those
  changes in the consuming controller repository.

## Safety impact

- [ ] Any retained firmware target in this repository remains receive-only;
      active CAN transmission is confined to the explicitly isolated bench ACK
      target.
- [ ] This repository does not own or build a make/model vehicle product;
      those artifacts belong in a consuming controller repository.
- [ ] Startup, stale-data, reset, and failure behavior remain fail-silent.
- [ ] No active CAN test target or BENCH_ACK_ONLY artifact can be mistaken for
      a vehicle build.
- [ ] Not applicable for a library/docs/tooling-only change; this PR cannot
      affect CAN, vehicle builds, or hardware. Explanation:

## Validation

### Automated

- [ ] Relevant host/unit/contract tests passed.
- [ ] Not run. Reason:

<!-- If this change moves or removes a product target, state the destination
repository and whether it has a configured publishing remote. -->

### Bench hardware

- [ ] Performed on the isolated CAN bench. Hardware/build/results:
- [ ] Not run. Reason:

### Vehicle

- [ ] Not applicable; this repository has no vehicle firmware artifact. Reason:
- [ ] Performed on a destination-repository vehicle target under its documented
      safety procedure. Results:
- [ ] Not run. Reason:

## Hardware and data evidence

<!-- Board revision, pins, bitrate, capture fixture, or signal provenance. -->

## Privacy and third-party material

- [ ] No credentials, VIN, precise location, or non-anonymized private trip data are included.
- [ ] No raw vehicle capture is included; raw captures are never accepted.
- [ ] Third-party code/data has source, exact version, and license recorded.
- [ ] If a reviewed anonymized fixture, generated signal definition, or third-party-derived artifact is included, I have completed the policy checklist and authorization statement.
- [ ] Not applicable.

## Risks, rollback, and follow-up

<!-- Known limitations, how to revert safely, and any follow-up issues. -->

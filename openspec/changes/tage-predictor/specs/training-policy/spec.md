## ADDED Requirements

### Requirement: use_alternate updated based on alternator correctness
After each branch resolution, `use_alternate` SHALL be incremented if the alternator predicted correctly, and decremented if the alternator predicted incorrectly. The counter SHALL saturate at its 5-bit limits.

#### Scenario: Alternator correct increments use_alternate
- **WHEN** the alternator's prediction matches the actual branch outcome
- **THEN** use_alternate++ (saturate at max)

#### Scenario: Alternator incorrect decrements use_alternate
- **WHEN** the alternator's prediction does not match the actual branch outcome
- **THEN** use_alternate-- (saturate at min)

### Requirement: Provider pred counter updated on TAGE table prediction
If the branch was predicted by a TAGE table (not bimodal), the provider's 3-bit pred counter SHALL be incremented if the branch was taken, and decremented if not-taken. The counter SHALL saturate (0..7).

#### Scenario: Taken branch increments provider pred counter
- **WHEN** a TAGE table was the provider AND the branch outcome is taken
- **THEN** provider's pred counter++ (saturate at 7)

#### Scenario: Not-taken branch decrements provider pred counter
- **WHEN** a TAGE table was the provider AND the branch outcome is not-taken
- **THEN** provider's pred counter-- (saturate at 0)

### Requirement: Alternator pred counter incremented when provider mispredicts as newly allocated
If the provider and alternator predicted differently AND the provider was wrong AND the provider is newly allocated (useful==1 AND pred counter is 3 or 4), the alternator's pred counter SHALL be incremented.

#### Scenario: Newly allocated provider mispredicts, alternator counter incremented
- **WHEN** provider prediction != alternator prediction AND provider is wrong AND provider has useful==1 AND pred counter ∈ {3,4}
- **THEN** alternator's pred counter++

### Requirement: Alternator pred counter decremented when provider predicts correctly and differently
If the provider and alternator predicted differently AND the provider was correct, the alternator's pred counter SHALL be decremented.

#### Scenario: Provider correct, alternator wrong, alternator counter decremented
- **WHEN** provider prediction != alternator prediction AND provider is correct
- **THEN** alternator's pred counter-- (saturate at 0)

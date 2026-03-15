## ADDED Requirements

### Requirement: Provider is longest-history matching table entry
The provider SHALL be the TAGE table entry with the longest history (highest table index T1..T8) whose tag matches the branch PC.

#### Scenario: Longest match selected as provider
- **WHEN** T3 and T5 both have tag-matching entries for a branch
- **THEN** T5 is selected as the provider

#### Scenario: No TAGE match falls back to bimodal
- **WHEN** no TAGE table has a matching tag entry
- **THEN** the bimodal base predictor SHALL be the provider

### Requirement: Alternator is second-longest matching entry
The alternator SHALL be the TAGE table entry with the second-longest history that has a tag match.

#### Scenario: Alternator selection
- **WHEN** T3, T5, and T7 all have tag matches
- **THEN** provider = T7, alternator = T5

#### Scenario: Only one TAGE match, alternator is bimodal
- **WHEN** exactly one TAGE table has a tag match
- **THEN** the alternator SHALL be the bimodal base predictor

### Requirement: use_alternate is a 5-bit global signed counter with threshold 15
The predictor SHALL maintain a single global `use_alternate` counter, 5 bits wide (range 0–31 or signed −16 to +15). The threshold for trusting the alternator is 15.

#### Scenario: use_alternate threshold check
- **WHEN** the provider is newly allocated AND use_alternate >= 15
- **THEN** the final prediction SHALL use the alternator's prediction instead of the provider's

#### Scenario: Provider not newly allocated always trusted
- **WHEN** the provider is NOT newly allocated (useful > 1 OR pred counter not in {3,4})
- **THEN** the final prediction SHALL use the provider's prediction regardless of use_alternate

### Requirement: Prediction counter MSB determines direction
The 3-bit prediction counter's MSB (bit 2) SHALL determine the predicted direction: 1 = taken, 0 = not-taken.

#### Scenario: Counter value 4 (100) predicts taken
- **WHEN** the provider's pred counter is 4 (binary 100)
- **THEN** the predicted direction is taken

#### Scenario: Counter value 3 (011) predicts not-taken
- **WHEN** the provider's pred counter is 3 (binary 011)
- **THEN** the predicted direction is not-taken

# Kontain Technology Demonstrator

## Abstract

Our technology demonstration is a serverless function platform that that allows high-performance 'always zero-scale' operation by leveraging KM snapshots. The overhead for KM snapshot startup is targeted at 10 ms of less (pulled that out of my ear).

Snapshot based Kontainer:

- Static by nature. All dynamic linkages have been performed before snapshot is taken.
- Immutable. Every run is a fresh instance of the snapshot memory image
- Isolated.

## Work Items

- Is an instantiated snapshot sufficient to represent function without bring FS along?
- How does request to function call and back work? Single augmented KM process? Multiple processes?
- Concurrency?
- How are function executables stored?
- How is this scaled up?
- Metrics? Chargebacks?
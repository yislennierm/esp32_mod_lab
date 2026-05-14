# Espressif GitHub Inventory Plan

Purpose: define how the project tracks Espressif repositories as a research source for ESP32-P4 Signal Lab development.

Status: canonical plan for external Espressif repository research.

Last updated: 2026-05-14.

## Objective

Maintain a strict inventory of Espressif public GitHub repositories so the lab can discover relevant SDK examples, tools, drivers, AI-agent work, display/camera pipelines, USB transports, and board-support patterns.

This matters because the lab should not depend only on memory or a few bookmarked repos. Espressif has hundreds of repositories, and some are indirectly useful even when they are not part of ESP-IDF.

## Current Understanding

GitHub currently reports 313 public repositories under the `espressif` organization.

The inventory must keep all repositories, including low-relevance entries. Relevance should prioritize research, not filter history away.

High-priority repository families for this project:

- ESP-IDF and SDK infrastructure
- display, LCD, camera, DSI, JPEG, PPA, PARLIO, and SPI examples
- USB device/host/transport projects
- board-support projects
- component registry examples
- AI-agent and code-generation projects such as `esp-claw`
- IDE/tooling projects that show how Espressif structures build/flash/debug workflows

`esp-claw` is interesting because it is Espressif's AI-agent framework for IoT devices. For this lab, its value is less about copying its runtime and more about studying how Espressif structures device-side AI interaction, command execution, local behavior loops, and user-facing automation.

Confidence level: high that the org inventory is useful. Medium that automatic relevance classification will be accurate without manual review.

## Unknowns

- Which repositories have current ESP32-P4 support.
- Which repositories are maintained versus historical.
- Which repositories depend on ESP-IDF master versus stable releases.
- Which examples are canonical versus experimental.
- Whether `esp-claw` or related AI-agent repositories can provide reusable ideas for device interaction without adding unnecessary complexity.

## Inventory Artifact

Generated inventory:

```text
inventories/espressif_github_repositories.json
```

Generator:

```text
host/espressif_github_inventory.py
```

The inventory records:

- repository name and URL
- description
- homepage
- language
- topics
- stars/forks/open issues
- archived/fork status
- default branch
- timestamps
- keyword categories
- relevance priority
- empty `research_notes` field for later human/AI annotation

Relevance values:

- `high`: likely useful soon
- `medium`: likely useful later or as reference
- `track`: still tracked, but not a priority

## Research Method

1. Regenerate the inventory from GitHub.
2. Review high-relevance repositories first.
3. Promote relevant repositories into focused research notes.
4. Link useful findings back to lab blocks, ESP-IDF descriptors, or project templates.
5. Keep low-relevance repositories in the inventory so they remain searchable.

The repo inventory complements, but does not replace, the local ESP-IDF SDK inventory.

## How This Fits The Lab

The lab has three knowledge sources:

- local ESP-IDF SDK inventory: concrete installed SDK files and examples
- Espressif GitHub inventory: broader official ecosystem and research map
- project profiles: what this lab has proven with real hardware

The UI should eventually expose the GitHub inventory as a research browser:

- filter by category
- show relevance
- open repository page
- attach repository evidence to a lab block
- mark a repository as researched
- import examples only when they are compatible with the local ESP-IDF project model

## Next Steps

- Generate the initial Espressif GitHub inventory.
- Add a backend endpoint for reading inventory summaries.
- Add a UI research tab after the SDK example importer exists.
- Manually inspect `esp-claw`, `esp-idf`, `esp-iot-solution`, display/camera repositories, USB repositories, and tooling repositories first.

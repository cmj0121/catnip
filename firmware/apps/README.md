# Built-in apps

An app that ships with the firmware rather than arriving on the card. It is the
same thing as an app under `/catnip/apps/` on SD — a `manifest.json`, a
`main.lua`, an `icon.png`, written against the same `ui.*` and `device.*` — and
that sameness is the point: a built-in app has no privileges, no private API and
no shortcut. If one of these needed something a card app cannot have, the honest
fix would be to give card apps that thing.

What differs is only where it comes from, and therefore what it can be relied on
for. The card is removable, so nothing that must always work can live there: the
File Browser is how you look at a card, which is not much use if it was on the
card you just took out.

Being built in is not a promotion. It is for the handful of apps the device
would be incomplete without, and every one of them here is a bet that this is
one of those — a bet worth re-examining when the list grows.

## What is here

| app           | why it is built in                                                  |
| ------------- | ------------------------------------------------------------------- |
| `filebrowser` | reading the card cannot depend on the card being there              |
| `clock`       | a device with a real-time clock has to be able to set it            |
| `scanner`     | the radios are the platform's; nothing else can show what they hear |

## Writing one

Read `docs/app-ui-spec.md` — the same document a card app is written against.
The File Browser is its worked example, so it is also the place to look for what
a well-behaved app does with the frame, the gestures and the back contract.

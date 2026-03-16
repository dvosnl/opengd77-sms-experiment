# HOW TO USE SMS

Compact overview of the current SMS features.

## Requirements

- Radio must be in DMR mode (digital).
- A valid own DMR ID is required.
- For sending: an active Private Call (PC) destination is required. (Double press '#')

## SMS Menu

- `SEND SMS`
- `INBOX`
- `QUICK TEXT`
- `SENT`

## Limits

- Message length: maximum 64 characters.
- Allowed characters: ASCII (plus CR/LF).
- Inbox storage: 8 messages.
- Sent storage: 8 messages.
- Quick Text storage: 10 templates.
- Quick Text title: maximum 16 characters.

## SEND SMS

1. Open `SEND SMS`.
2. Type the message.
3. `Green` = send.

Behavior:

- The message is stored in `SENT`.
- If ACK wait mode is enabled, you will see ACK/timeout status.

## INBOX

- `Green`: open message.
- `#`: delete selected message.
- In message view: `#` deletes the open message.

## SENT

- `Green`: open message.
- `#`: delete selected message.
- In message view: `#` deletes the open message.
- In message view: long press `6` = resend.

## QUICK TEXT

- `0`: new template.
  - Step 1: message text.
  - Step 2: title.
- `Green`: use the selected template (prefill in `SEND SMS`).
- `#`: delete the selected template.
- Long press `3`: edit the selected template.

## SMS Options

In `SMS options`:

- `Wait for ACK` (`On/Off`)
  - `On`: waits for an ACK/timeout event.
  - `Off`: sends without waiting for ACK.
- `In filter` (`None/PC`)
  - `PC`: filters incoming Private Calls.

## Main Messages

- `DMR only`: not in digital mode.
- `Select private call`: no valid PC destination selected.
- `SMS busy`: transmit path busy.
- `Empty message`: empty message.
- `Message too long`: more than 64 characters.
- `ASCII only`: unsupported characters were used.

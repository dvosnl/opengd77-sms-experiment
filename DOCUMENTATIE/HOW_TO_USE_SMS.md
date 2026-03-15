# HOW TO USE SMS

This guide is radio-independent and applies to the current SMS implementation in the firmware.

## 1. Prerequisites

- Put the radio in DMR (digital) mode.
- Make sure your transmitter ID (DMR ID) is correctly configured.
- For sending, a Private Call (PC) destination must be active.

## 2. Opening the SMS menu

- Open the menu and navigate to SMS.
- You will see three sections:
  - SEND SMS
  - INBOX
  - SENT

## 3. Sending a message (SEND SMS)

1. Select `SEND SMS`.
2. Type your message (maximum 64 characters).
3. Verify that a Private Call destination is active.
4. Press `Green` to send.

What you will see:

- `SMS TX` upon successful start of transmission.
- The message is automatically saved in `SENT`.

## 4. Using INBOX

### Viewing messages

1. Go to `INBOX`.
2. Select a message.
3. Press `Green` to open it.

### Deleting messages

- In the list: press `#` to delete the selected message.
- In the message view: press `#` to delete the open message.

## 5. Using SENT

### Viewing sent messages

1. Go to `SENT`.
2. Select a message.
3. Press `Green` to open it.

### Resending a message

- Open a message in `SENT`.
- Hold `6` to resend the same message.

### Deleting sent messages

- In the list: press `#` to delete the selected message.
- In the message view: press `#` to delete the open message.

## 6. Shortcut overview

- `Green`: open / confirm / send
- `Red`: back
- `#`: delete (INBOX and SENT)
- Long press `6`: resend in SENT message view

## 7. Error messages and their meaning

- `DMR only`: the radio is not in digital mode.
- `Select private call`: no valid private destination has been selected.
- `SMS busy`: the transmit path is busy; try again.
- `Empty message`: the message text is empty.
- `Message too long`: the message exceeds 64 characters.
- `ASCII only`: only ASCII characters are supported in this implementation.

## 8. Practical tips

- Start with short messages (e.g. "test") for initial testing.
- Use a stable private call setup for reliable transmission.
- Delete old messages with `#` to keep things organised.

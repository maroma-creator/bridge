# Bridge

A two-way-ish bridge for deaf users, in a single HTML file.

- **Speech → Sign:** speak (Arabic or English) and an avatar replays signs you recorded with your own webcam (MediaPipe hand + body tracking).
- **Feel the Sound:** the mic detects bangs, alarms/doorbells, rumbles, speech, and your name, and turns each into a distinct vibration pattern (real vibration on Android Chrome, simulated wristband elsewhere).

## Run it
Open `index.html` in Chrome or Edge and allow camera/mic. Needs internet the first time to download the hand-tracking models.
Or enable GitHub Pages (Settings → Pages → main / root) to use it from your phone over https.

## Notes
The avatar only knows signs you record, and signs exactly like whoever recorded them. For real Saudi Sign Language, record with a signer who knows it.

## Next
Arduino wristband (mic + vibration motor) so the buzz happens on the wrist.

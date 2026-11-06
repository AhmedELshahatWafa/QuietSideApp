# Coworking Space Manager

Native C++ / Qt 6 desktop application for managing a physical coworking space locally on Windows.

## Features
- Client data management (SQLite)
- Complimentary free time can be added to a selected client's time bank from the Clients tab
- Pay-as-you-go sessions with no required time balance
- Checkout billing at 0.50 EGP per started minute, with a 30 EGP minimum per visit
- Optional reserved-hour sessions using the 1-hour, 2-hour, 4-hour, and 8-hour packages
- Reserved sessions pause at their time limit and require checkout or an upgrade to a longer package
- Checking out a reserved session early credits its unused whole minutes to the client's time bank
- Cash, card, bank transfer, or other payment-method recording at checkout
- Checkout can confirm payment received or record the unpaid amount as an outstanding invoice
- Session completion and its payment or debt record are saved atomically
- Payments tab shows checkout payment history; old time-bank balances remain visible
  as legacy data and are not changed by the new billing flow
- The In Debt tab lists unpaid visits and legacy negative balances; confirming payment
  records the receipt and clears the outstanding debt
- Payments tab shows the cumulative total of recorded payments
- Session pause/resume and reception status updates
- Active and paused sessions are saved locally and restored after the app restarts;
  active-session time continues to elapse while the app is closed, while paused
  sessions stay paused
- Active alerts are saved locally and restored after the app restarts
- The existing dark appearance is preserved as the default theme; a saved theme
  toggle switches to a warm cream, brown, and gold palette inspired by the
  QuietSide brand artwork
- The app opens full-screen in a frameless window without the Windows title bar
- The reserved-time timeout sound repeats every 10 seconds for expired sessions
  and stops after checkout or reservation upgrade; the Alerts panel includes
  a control to silence current timeout alerts; test the sound with Ctrl+Shift+B
- Visual lobby / floor plan with live avatars
- Extensible financial foundation (transactions, invoices, packages)

## Build (Windows, VS Code)

### Prerequisites
- CMake 3.16+
- Qt 6 (Widgets, Sql, Network) installed (e.g. via Qt Online Installer)
- A C++17 compiler (MSVC 2019/2022 or MinGW)
- VS Code with the "CMake Tools" and "C/C++" extensions

### Configure & Build
1. Open the project folder in VS Code.
2. Install extensions: CMake Tools, C/C++.
3. Select a kit (MSVC or MinGW) via "CMake: Select a Kit".
4. Run "CMake: Configure" (or `cmake -S . -B build`).
5. Run "CMake: Build" (or `cmake --build build --config Release`).
6. The executable and its required Qt runtime files are produced in the build
   directory (for example, `build/CoworkingSpaceManager.exe`).

### Run
Launch the executable from the build directory; keep the accompanying Qt DLLs and
`platforms` folder beside it. The SQLite database is created automatically in
`%APPDATA%/CoworkingSpace/Coworking Space Manager/coworking.db`.

### Notes
- First run seeds default rooms (Lounge, Main Room, Small Room, Balcony).
- The background timer persists billable session time every 5 seconds. Partial minutes
  are rounded up at checkout, and paused time is not billed.

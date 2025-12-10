# Multi-client-chat-server
A TCP-based multi-client chat server built in C, supporting real-time messaging, multiple simultaneous connections, user commands, and threaded communication. Implements client IDs, message broadcasting, private messaging, and smooth connection handling—all running directly in the terminal.

🚀 Built a Multi-Client Chat Server in C!

Today I worked on implementing a TCP-based multi-client chat server entirely in C — running directly on the terminal.
Multiple clients can connect simultaneously, send and receive messages in real-time, and interact using custom commands like:

/name <newName> – change your display name

/list – see active users

/msg <id> <text> – private messaging

/quit – disconnect safely

The server handles:
✔ Multiple clients using threads
✔ Concurrent message broadcasting
✔ Clean connection handling
✔ Logging + server-side monitoring
✔ A simple but functional command system

Even though I'm running everything locally (not hosted yet), the communication flow works exactly like a real chat application — client connects → gets ID → server manages all interactions.

This project helped me strengthen my understanding of:
🔹 Sockets & TCP connections
🔹 Multi-threading in C
🔹 Synchronization & concurrency concepts
🔹 Low-level client–server architecture
🔹 How real chat apps manage users behind the scenes

It’s always fun to see code come alive as actual conversations happening inside the terminal!

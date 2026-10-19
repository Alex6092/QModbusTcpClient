# SimpleModbusTcpClient

> Version française : [README.fr.md](README.fr.md)

A small asynchronous **Modbus TCP client for Qt 5 and Qt 6**. It is two files to add to your
project. Results arrive as Qt signals, and errors are fully handled: Modbus exceptions,
timeouts, connection loss, malformed answers and invalid parameters.

- [Features](#features)
- [Adding it to your project](#adding-it-to-your-project)
- [Quick start](#quick-start)
- [Complete example](#complete-example)
- [Error handling](#error-handling)
- [Configuration](#configuration)
- [Modbus addresses](#modbus-addresses)
- [Behaviour and guarantees](#behaviour-and-guarantees)
- [Running the tests](#running-the-tests)
- [Migrating from QModbusTcpClient](#migrating-from-qmodbustcpclient)

## Features

| FC | Method | Success signal | Limits |
|------|-------------------------------------------|----------------------------------------------------|-------------|
| 0x01 | `readCoils(startAddress, count)` | `coilsRead(transactionId, startAddress, values)` | 1 to 2000 |
| 0x02 | `readDiscreteInputs(startAddress, count)` | `discreteInputsRead(transactionId, startAddress, values)` | 1 to 2000 |
| 0x03 | `readHoldingRegisters(startAddress, count)` | `holdingRegistersRead(transactionId, startAddress, values)` | 1 to 125 |
| 0x04 | `readInputRegisters(startAddress, count)` | `inputRegistersRead(transactionId, startAddress, values)` | 1 to 125 |
| 0x05 | `writeSingleCoil(address, value)` | `singleCoilWritten(transactionId, address, value)` | |
| 0x06 | `writeSingleRegister(address, value)` | `singleRegisterWritten(transactionId, address, value)` | |
| 0x0F | `writeMultipleCoils(startAddress, values)` | `multipleCoilsWritten(transactionId, startAddress, values)` | 1 to 1968 |
| 0x10 | `writeMultipleRegisters(startAddress, values)` | `multipleRegistersWritten(transactionId, startAddress, values)` | 1 to 123 |

- A single `requestFailed()` signal reports every failure, with the Modbus exception code when there is one.
- The timeout, the unit identifier (slave id) and the maximum number of simultaneous requests (a queue) can be configured.
- Answers are parsed robustly: fragmented, grouped or out-of-order answers are handled, every answer is checked against its request, and a frame error never blocks the client.
- Qt 5.9 or later, or Qt 6. The only dependency is the Qt Network module, and a C++11 compiler is enough.

## Adding it to your project

Copy `simplemodbustcpclient.h` and `simplemodbustcpclient.cpp` into your project, or add this
repository as a git submodule:

```sh
git submodule add https://github.com/Alex6092/QModbusTcpClient.git third_party/SimpleModbusTcpClient
```

### qmake

Include the provided `.pri` file in your `.pro` file:

```qmake
include(third_party/SimpleModbusTcpClient/simplemodbustcpclient.pri)
```

Or add the files by hand:

```qmake
QT += network
HEADERS += simplemodbustcpclient.h
SOURCES += simplemodbustcpclient.cpp
```

### CMake

Add the directory. It provides the `SimpleModbusTcpClient` static library target, and uses the
same Qt version as your project:

```cmake
add_subdirectory(third_party/SimpleModbusTcpClient)
target_link_libraries(myapp PRIVATE SimpleModbusTcpClient)
```

Or add the files by hand. `AUTOMOC` is required because the class uses `Q_OBJECT`:

```cmake
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Network)   # or Qt5
add_executable(myapp main.cpp simplemodbustcpclient.cpp simplemodbustcpclient.h)
target_link_libraries(myapp PRIVATE Qt6::Network)
```

## Quick start

```cpp
#include "simplemodbustcpclient.h"

auto *modbus = new SimpleModbusTcpClient("192.168.1.10", 502, this);
modbus->setUnitId(1);

connect(modbus, &SimpleModbusTcpClient::connected, this, [modbus]() {
    modbus->readHoldingRegisters(0, 10);   // Registers 40001 to 40010
    modbus->writeSingleCoil(4, true);      // Coil 00005
});

connect(modbus, &SimpleModbusTcpClient::holdingRegistersRead, this,
        [](quint16 /*transactionId*/, quint16 startAddress, const QVector<quint16> &values) {
    qDebug() << "Registers from address" << startAddress << ":" << values;
});

connect(modbus, &SimpleModbusTcpClient::singleCoilWritten, this,
        [](quint16 /*transactionId*/, quint16 address, bool value) {
    qDebug() << "Coil" << address << "set to" << value;
});

connect(modbus, &SimpleModbusTcpClient::requestFailed, this,
        [](quint16 transactionId, quint8 functionCode, quint16 address,
           SimpleModbusTcpClient::Error error, quint8 exceptionCode) {
    qWarning() << "Request" << transactionId << "( FC" << functionCode << ", address" << address
               << ") failed:" << SimpleModbusTcpClient::errorString(error);
    if (error == SimpleModbusTcpClient::ExceptionError)
        qWarning() << "Modbus exception:" << SimpleModbusTcpClient::exceptionString(exceptionCode);
});

modbus->connectToHost();
```

## Complete example

This class polls ten input registers every 500 ms, writes a setpoint on request, reports
errors and reconnects automatically when the connection is lost.

```cpp
// devicemonitor.h
#include <QObject>
#include <QTimer>
#include "simplemodbustcpclient.h"

class DeviceMonitor : public QObject
{
    Q_OBJECT

public:
    explicit DeviceMonitor(const QString &host, QObject *parent = nullptr);
    void setSetpoint(quint16 value);

signals:
    void measuresUpdated(const QVector<quint16> &measures);
    void setpointWritten(bool success);

private:
    void poll();
    void onRequestFailed(quint16 transactionId, quint8 functionCode, quint16 address,
                         SimpleModbusTcpClient::Error error, quint8 exceptionCode);

    SimpleModbusTcpClient m_modbus;
    QTimer m_pollTimer;
    QTimer m_reconnectTimer;
    quint16 m_setpointRequest = 0;
};
```

```cpp
// devicemonitor.cpp
#include "devicemonitor.h"
#include <QDebug>

DeviceMonitor::DeviceMonitor(const QString &host, QObject *parent)
    : QObject(parent), m_modbus(host, 502)
{
    m_modbus.setUnitId(1);
    m_modbus.setTimeout(500);
    m_modbus.setMaxConcurrentRequests(1);   // The safest choice for an unknown device

    m_pollTimer.setInterval(500);
    connect(&m_pollTimer, &QTimer::timeout, this, &DeviceMonitor::poll);

    m_reconnectTimer.setInterval(2000);
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, [this]() { m_modbus.connectToHost(); });

    connect(&m_modbus, &SimpleModbusTcpClient::connected, this, [this]() {
        m_pollTimer.start();
        poll();
    });

    // Connection lost, or connection attempt failed: try again later
    connect(&m_modbus, &SimpleModbusTcpClient::stateChanged, this, [this](QAbstractSocket::SocketState state) {
        if (state == QAbstractSocket::UnconnectedState) {
            m_pollTimer.stop();
            m_reconnectTimer.start();
        }
    });

    connect(&m_modbus, &SimpleModbusTcpClient::socketError, this,
            [](QAbstractSocket::SocketError, const QString &message) {
        qWarning() << "Socket error:" << message;
    });

    connect(&m_modbus, &SimpleModbusTcpClient::inputRegistersRead, this,
            [this](quint16, quint16, const QVector<quint16> &values) {
        emit measuresUpdated(values);
    });

    connect(&m_modbus, &SimpleModbusTcpClient::singleRegisterWritten, this,
            [this](quint16 transactionId, quint16, quint16) {
        if (transactionId == m_setpointRequest)
            emit setpointWritten(true);
    });

    connect(&m_modbus, &SimpleModbusTcpClient::requestFailed, this, &DeviceMonitor::onRequestFailed);

    m_modbus.connectToHost();
}

void DeviceMonitor::setSetpoint(quint16 value)
{
    // Keep the transaction id to recognize the answer
    m_setpointRequest = m_modbus.writeSingleRegister(100, value);   // Register 40101
}

void DeviceMonitor::poll()
{
    // Do not pile requests up when the device is slower than the polling period
    if (m_modbus.pendingRequestCount() == 0)
        m_modbus.readInputRegisters(0, 10);   // Registers 30001 to 30010
}

void DeviceMonitor::onRequestFailed(quint16 transactionId, quint8 functionCode, quint16 address,
                                    SimpleModbusTcpClient::Error error, quint8 exceptionCode)
{
    QString reason = SimpleModbusTcpClient::errorString(error);
    if (error == SimpleModbusTcpClient::ExceptionError)
        reason += " (" + SimpleModbusTcpClient::exceptionString(exceptionCode) + ")";

    qWarning("Modbus request %u (FC 0x%02X, address %u) failed: %s",
             transactionId, functionCode, address, qPrintable(reason));

    if (transactionId == m_setpointRequest)
        emit setpointWritten(false);
}
```

## Error handling

### The contract

- Every request method returns the **transaction id** of the request, which is never 0.
- Later on, **exactly one signal** is emitted for this request: its success signal, or `requestFailed()`.
- These signals are **never emitted from inside the request method**, so the returned id can always be stored before the result arrives.
- A write success signal is emitted only once the device has confirmed the write. The echo in the answer is checked against the request.
- A read success signal always holds exactly the requested number of values.

```cpp
void requestFailed(quint16 transactionId, quint8 functionCode, quint16 address,
                   SimpleModbusTcpClient::Error error, quint8 exceptionCode);
```

`address` is the (start) address of the request, and `exceptionCode` is only set when `error` is
`ExceptionError`. `SimpleModbusTcpClient::errorString(error)` and
`SimpleModbusTcpClient::exceptionString(exceptionCode)` give a readable description.

### Errors

| `Error` | Meaning |
|------------------------|------------------------------------------------------------------------------|
| `ExceptionError` | The device answered with a Modbus exception, see `exceptionCode` |
| `TimeoutError` | No answer within `timeout()`. A late answer is ignored |
| `NotConnectedError` | The request was issued while the client was not connected (nothing is sent) |
| `ConnectionLostError` | The connection was closed, or could not be opened, before the answer arrived |
| `InvalidRequestError` | Parameters outside of the Modbus limits (nothing is sent) |
| `InvalidResponseError` | Malformed answer, or an answer that does not match the request (byte count, echo, function code) |
| `WriteError` | The request could not be written to the socket |

### Modbus exception codes

| Code | `ExceptionCode` | Usual cause |
|------|---------------------------------------|---------------------------------------------------------------|
| 0x01 | `IllegalFunction` | The function code is not supported by the device |
| 0x02 | `IllegalDataAddress` | The address, or address + count, is outside the device's map |
| 0x03 | `IllegalDataValue` | A value in the request is not accepted (quantity, value...) |
| 0x04 | `ServerDeviceFailure` | Unrecoverable error inside the device |
| 0x05 | `Acknowledge` | Long request accepted, still being processed |
| 0x06 | `ServerDeviceBusy` | The device is busy, try again later |
| 0x07 | `NegativeAcknowledge` | The device cannot perform the requested program function |
| 0x08 | `MemoryParityError` | Memory consistency error in the device |
| 0x0A | `GatewayPathUnavailable` | The gateway has no path to the target (configuration, overload) |
| 0x0B | `GatewayTargetDeviceFailedToRespond` | The device behind the gateway did not answer (check the unit id) |

Devices can also answer with vendor specific codes, which is why `exceptionCode` is a `quint8`.

Socket-level errors (connection refused, host not found...) are forwarded by the
`socketError(QAbstractSocket::SocketError, QString)` signal.

## Configuration

| Setting | Default | Description |
|----------------------------------|---------|-------------|
| `setUnitId(quint8)` | `0` | Unit identifier (slave id) put in the requests made after the call. Use the slave address with a Modbus TCP to RTU gateway. To reach several slaves through one gateway, call it before each request |
| `setTimeout(int msec)` | `1000` | Maximum time to wait for an answer, counted from the moment the request is sent. `0` or less disables the timeout |
| `setMaxConcurrentRequests(int)` | `0` | Maximum number of requests sent without having received their answer. The next ones wait in a queue. `0` means no limit |
| `setHost()` / `setPort()` | | Used by the next `connectToHost()` |

> **Which value for `setMaxConcurrentRequests()`?** Modbus TCP allows several requests in
> flight at once, but many devices and gateways only answer the first of several requests
> received together and silently drop the others (this is the case of the `pymodbus` server,
> for instance). The dropped requests then end with `TimeoutError`. If you see this, or if
> you do not know the device, use `setMaxConcurrentRequests(1)`.

Other useful functions:

- `connectToHost()`, `connectToHost(host, port)`, `disconnectFromHost()`, `state()` and `isConnected()`.
- The signals `connected()`, `disconnected()`, `stateChanged()` and `socketError()`.
- `pendingRequestCount()`: the number of requests whose signal has not been emitted yet. Useful to avoid piling requests up.
- `socket()`: the internal `QTcpSocket`, **for advanced settings only**. Never read from it or write to it.

  ```cpp
  modbus->socket()->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
  ```

- Messages are logged in the `simplemodbus.tcpclient` category. For example, `QT_LOGGING_RULES="simplemodbus.tcpclient.warning=false"` silences the warnings.

## Modbus addresses

The methods use the **protocol addresses, starting at 0**. Device documentations often use the
"Modicon" notation, where the first digit gives the table and the numbering starts at 1:

| Table | Notation | Methods | Example |
|-------------------|----------------|-----------------------------------------------------|----------------------------------|
| Coils | 00001 to 09999 | `readCoils`, `writeSingleCoil`, `writeMultipleCoils` | Coil 00005 is address 4 |
| Discrete inputs | 10001 to 19999 | `readDiscreteInputs` | Input 10001 is address 0 |
| Input registers | 30001 to 39999 | `readInputRegisters` | Register 30010 is address 9 |
| Holding registers | 40001 to 49999 | `readHoldingRegisters`, `writeSingleRegister`, `writeMultipleRegisters` | Register 40108 is address 107 |

Registers are 16-bit values. A 32-bit value uses two registers, and the word order depends on the device:

```cpp
const quint32 raw = (quint32(values[0]) << 16) | values[1];   // High word first
float measure;
memcpy(&measure, &raw, sizeof(measure));                       // #include <cstring>
```

## Behaviour and guarantees

- Everything is asynchronous and nothing blocks. Use the client from the thread that owns it, which must run an event loop.
- Requests issued while connecting are queued and sent as soon as the connection is established.
- Requests are sent in the order they are made. Answers can arrive in any order and are matched to their request by transaction id.
- When the connection closes, every pending request (sent or queued) ends with `ConnectionLostError`, and the receive buffer is cleared before the next connection.
- An invalid frame header (protocol id other than 0, or an impossible length) clears the receive buffer instead of blocking the client.
- Destroying the client silently discards its pending requests: no signal is emitted.

## Running the tests

The `tests/` directory contains unit tests (Qt Test) that run against a fake Modbus server:

```sh
cmake -S . -B build
cmake --build build
cd build && ctest --output-on-failure
```

Qt 6 is used when it is available. Add `-DSIMPLEMODBUSTCPCLIENT_QT_VERSION=5` to the first
command to force Qt 5.

## Migrating from QModbusTcpClient

This class replaces the former `QModbusTcpClient`. The new name avoids the clash with the
`QModbusTcpClient` class of the Qt Serial Bus module. The API was redesigned:

| Before | Now |
|--------|-----|
| `#include "qmodbustcpclient.h"`, `QModbusTcpClient` | `#include "simplemodbustcpclient.h"`, `SimpleModbusTcpClient` |
| Inherits `QTcpSocket` | Inherits `QObject`. Use `connected()`, `disconnected()`, `stateChanged()`, `socketError()`, `state()`, `isConnected()` and `socket()` |
| Unit id always 0 | `setUnitId()` |
| (missing) | `readCoils()` / `coilsRead()` (FC 0x01) |
| `readMultipleInputsStatusFC2()` / `onReadMultipleInputsStatusSentence(start, values)` | `readDiscreteInputs()` / `discreteInputsRead(transactionId, start, values)` |
| `readMultipleHoldingRegistersFC3()` / `onReadMultipleHoldingRegistersSentence(start, values)` | `readHoldingRegisters()` / `holdingRegistersRead(transactionId, start, values)` |
| `readMultipleInputRegistersFC4()` / `onReadMultipleInputRegistersSentence(start, values)` | `readInputRegisters()` / `inputRegistersRead(transactionId, start, values)` |
| `on...SentenceSingleValue(address, value)` | Removed: loop over `values` (the address of `values[i]` is `start + i`) |
| `forceSingleCoilFC5()` / `onForceSingleCoilSentence(success, address, value)` | `writeSingleCoil()` / `singleCoilWritten(transactionId, address, value)` |
| `writeSingleWordFC6()` / `onWriteSingleWordSentence(success, address, value)` | `writeSingleRegister()` / `singleRegisterWritten(transactionId, address, value)` |
| `forceMultipleCoilsFC15()` / `onForceMultipleCoilsSentence(success, start, values, count)` | `writeMultipleCoils()` / `multipleCoilsWritten(transactionId, start, values)` |
| `presetMultipleRegistersFC16()` / `onPresetMultipleRegistersSentence(success, start, values, count)` | `writeMultipleRegisters()` / `multipleRegistersWritten(transactionId, start, values)` |
| `success == false` | `requestFailed()`, which now also reports exceptions, timeouts and disconnections |

The redesign also fixes several bugs of the former version:

- An answer whose length byte was 0x80 or more made the reception loop forever. This happened, for instance, when reading 63 registers or more.
- FC 0x10 sent only part of the frame, with a wrong byte count. It never worked and desynchronized the connection.
- FC 0x0F with an empty list overflowed a buffer.
- Exception answers and unanswered requests were silently ignored, and the latter leaked memory.
- After a reconnection, an incomplete answer left in the buffer corrupted the following answers.

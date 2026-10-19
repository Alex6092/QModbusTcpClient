#include "simplemodbustcpclient.h"

#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QTcpSocket>
#include <QTimer>

Q_LOGGING_CATEGORY(lcSimpleModbus, "simplemodbus.tcpclient")

namespace {

const int MbapHeaderSize = 7;       // Transaction id, protocol id, length, unit id
const int MaxMbapLength = 254;      // Unit id + 253 bytes PDU
const int MaxReadBits = 2000;
const int MaxReadRegisters = 125;
const int MaxWriteBits = 1968;
const int MaxWriteRegisters = 123;

void appendUInt16(QByteArray &data, quint16 value)
{
    data.append(char(value >> 8));
    data.append(char(value & 0xFF));
}

quint16 readUInt16(const QByteArray &data, int index)
{
    return quint16((quint8(data.at(index)) << 8) | quint8(data.at(index + 1)));
}

bool isValidRange(qint64 startAddress, qint64 count, qint64 maxCount)
{
    return count >= 1 && count <= maxCount && startAddress + count <= 0x10000;
}

// Bits are packed LSB first: value 0 is bit 0 of the first byte.
QByteArray packBits(const QVector<bool> &values)
{
    QByteArray bytes(int((values.size() + 7) / 8), '\0');
    for (int i = 0; i < values.size(); i++)
    {
        if (values.at(i))
            bytes[i / 8] = char(quint8(bytes.at(i / 8)) | (1 << (i % 8)));
    }
    return bytes;
}

QVector<bool> unpackBits(const QByteArray &bytes, int offset, int count)
{
    QVector<bool> values;
    values.reserve(count);
    for (int i = 0; i < count; i++)
        values.append(((quint8(bytes.at(offset + i / 8)) >> (i % 8)) & 0x01) != 0);
    return values;
}

QByteArray buildFrame(quint16 transactionId, quint8 unitId, const QByteArray &pdu)
{
    QByteArray frame;
    frame.reserve(MbapHeaderSize + pdu.size());
    appendUInt16(frame, transactionId);
    appendUInt16(frame, 0); // Protocol id
    appendUInt16(frame, quint16(pdu.size() + 1));
    frame.append(char(unitId));
    frame.append(pdu);
    return frame;
}

} // namespace

struct SimpleModbusTcpClient::Request
{
    quint16 transactionId = 0;
    quint8 functionCode = 0;
    quint16 address = 0;
    quint16 count = 0;
    QVector<bool> coils;            // Values to write (FC 0x05 and 0x0F)
    QVector<quint16> registers;     // Values to write (FC 0x06 and 0x10)
    QByteArray frame;
    QElapsedTimer sentTimer;
    Error error = NoError;
    quint8 exceptionCode = NoException;
};

SimpleModbusTcpClient::SimpleModbusTcpClient(QObject *parent)
    : SimpleModbusTcpClient(QString(), 502, parent)
{
}

SimpleModbusTcpClient::SimpleModbusTcpClient(const QString &host, quint16 port, QObject *parent)
    : QObject(parent),
      m_socket(new QTcpSocket(this)),
      m_timeoutTimer(new QTimer(this)),
      m_host(host),
      m_port(port),
      m_unitId(0),
      m_timeout(1000),
      m_maxConcurrentRequests(0),
      m_transactionId(0),
      m_failureReportScheduled(false)
{
    // Needed by queued connections to our signals
    qRegisterMetaType<SimpleModbusTcpClient::Error>("SimpleModbusTcpClient::Error");
    qRegisterMetaType<QVector<bool> >("QVector<bool>");
    qRegisterMetaType<QVector<quint16> >("QVector<quint16>");
    qRegisterMetaType<QAbstractSocket::SocketState>("QAbstractSocket::SocketState");
    qRegisterMetaType<QAbstractSocket::SocketError>("QAbstractSocket::SocketError");

    connect(m_socket, &QTcpSocket::readyRead, this, &SimpleModbusTcpClient::onReadyRead);
    connect(m_socket, &QTcpSocket::stateChanged, this, &SimpleModbusTcpClient::onStateChanged);
    connect(m_socket, &QTcpSocket::connected, this, &SimpleModbusTcpClient::connected);
    connect(m_socket, &QTcpSocket::disconnected, this, &SimpleModbusTcpClient::disconnected);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(m_socket, &QTcpSocket::errorOccurred, this, &SimpleModbusTcpClient::onSocketError);
#else
    connect(m_socket, static_cast<void (QAbstractSocket::*)(QAbstractSocket::SocketError)>(&QAbstractSocket::error),
            this, &SimpleModbusTcpClient::onSocketError);
#endif
    connect(m_timeoutTimer, &QTimer::timeout, this, &SimpleModbusTcpClient::checkTimeouts);
}

SimpleModbusTcpClient::~SimpleModbusTcpClient()
{
    // The socket is a child object: it is destroyed after this destructor and
    // aborts its connection, so it must not call us back anymore.
    m_socket->disconnect(this);

    qDeleteAll(m_waitingRequests);
    qDeleteAll(m_sentRequests);
    qDeleteAll(m_failedRequests);
}

// Connection :

void SimpleModbusTcpClient::setHost(const QString &host)
{
    m_host = host;
}

QString SimpleModbusTcpClient::host() const
{
    return m_host;
}

void SimpleModbusTcpClient::setPort(quint16 port)
{
    m_port = port;
}

quint16 SimpleModbusTcpClient::port() const
{
    return m_port;
}

void SimpleModbusTcpClient::connectToHost()
{
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
    {
        qCDebug(lcSimpleModbus, "connectToHost() ignored, the client is not unconnected (state %d)", int(m_socket->state()));
        return;
    }

    m_socket->connectToHost(m_host, m_port);
}

void SimpleModbusTcpClient::connectToHost(const QString &host, quint16 port)
{
    m_host = host;
    m_port = port;
    connectToHost();
}

void SimpleModbusTcpClient::disconnectFromHost()
{
    m_socket->disconnectFromHost();
}

QAbstractSocket::SocketState SimpleModbusTcpClient::state() const
{
    return m_socket->state();
}

bool SimpleModbusTcpClient::isConnected() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

QTcpSocket *SimpleModbusTcpClient::socket() const
{
    return m_socket;
}

// Settings :

void SimpleModbusTcpClient::setUnitId(quint8 unitId)
{
    m_unitId = unitId;
}

quint8 SimpleModbusTcpClient::unitId() const
{
    return m_unitId;
}

void SimpleModbusTcpClient::setTimeout(int msec)
{
    m_timeout = msec;
    updateTimeoutTimer();
}

int SimpleModbusTcpClient::timeout() const
{
    return m_timeout;
}

void SimpleModbusTcpClient::setMaxConcurrentRequests(int count)
{
    m_maxConcurrentRequests = qMax(0, count);
    sendPendingRequests();
}

int SimpleModbusTcpClient::maxConcurrentRequests() const
{
    return m_maxConcurrentRequests;
}

int SimpleModbusTcpClient::pendingRequestCount() const
{
    return int(m_waitingRequests.size() + m_sentRequests.size() + m_failedRequests.size());
}

// Requests :

quint16 SimpleModbusTcpClient::readCoils(quint16 startAddress, quint16 count)
{
    return sendReadRequest(ReadCoils, startAddress, count, MaxReadBits);
}

quint16 SimpleModbusTcpClient::readDiscreteInputs(quint16 startAddress, quint16 count)
{
    return sendReadRequest(ReadDiscreteInputs, startAddress, count, MaxReadBits);
}

quint16 SimpleModbusTcpClient::readHoldingRegisters(quint16 startAddress, quint16 count)
{
    return sendReadRequest(ReadHoldingRegisters, startAddress, count, MaxReadRegisters);
}

quint16 SimpleModbusTcpClient::readInputRegisters(quint16 startAddress, quint16 count)
{
    return sendReadRequest(ReadInputRegisters, startAddress, count, MaxReadRegisters);
}

quint16 SimpleModbusTcpClient::writeSingleCoil(quint16 address, bool value)
{
    Request *request = new Request;
    request->functionCode = WriteSingleCoil;
    request->address = address;
    request->count = 1;
    request->coils.append(value);

    QByteArray pdu;
    pdu.append(char(WriteSingleCoil));
    appendUInt16(pdu, address);
    appendUInt16(pdu, value ? 0xFF00 : 0x0000);
    return enqueueRequest(request, pdu, true);
}

quint16 SimpleModbusTcpClient::writeSingleRegister(quint16 address, quint16 value)
{
    Request *request = new Request;
    request->functionCode = WriteSingleRegister;
    request->address = address;
    request->count = 1;
    request->registers.append(value);

    QByteArray pdu;
    pdu.append(char(WriteSingleRegister));
    appendUInt16(pdu, address);
    appendUInt16(pdu, value);
    return enqueueRequest(request, pdu, true);
}

quint16 SimpleModbusTcpClient::writeMultipleCoils(quint16 startAddress, const QVector<bool> &values)
{
    const bool valid = isValidRange(startAddress, values.size(), MaxWriteBits);

    Request *request = new Request;
    request->functionCode = WriteMultipleCoils;
    request->address = startAddress;
    request->count = quint16(valid ? values.size() : 0);
    request->coils = values;

    QByteArray pdu;
    if (valid)
    {
        const QByteArray bits = packBits(values);
        pdu.append(char(WriteMultipleCoils));
        appendUInt16(pdu, startAddress);
        appendUInt16(pdu, request->count);
        pdu.append(char(bits.size()));
        pdu.append(bits);
    }
    return enqueueRequest(request, pdu, valid);
}

quint16 SimpleModbusTcpClient::writeMultipleRegisters(quint16 startAddress, const QVector<quint16> &values)
{
    const bool valid = isValidRange(startAddress, values.size(), MaxWriteRegisters);

    Request *request = new Request;
    request->functionCode = WriteMultipleRegisters;
    request->address = startAddress;
    request->count = quint16(valid ? values.size() : 0);
    request->registers = values;

    QByteArray pdu;
    if (valid)
    {
        pdu.append(char(WriteMultipleRegisters));
        appendUInt16(pdu, startAddress);
        appendUInt16(pdu, request->count);
        pdu.append(char(2 * request->count));
        for (quint16 value : values)
            appendUInt16(pdu, value);
    }
    return enqueueRequest(request, pdu, valid);
}

QString SimpleModbusTcpClient::errorString(Error error)
{
    switch (error)
    {
    case NoError:
        return tr("No error");
    case ExceptionError:
        return tr("The device answered with a Modbus exception");
    case TimeoutError:
        return tr("No answer from the device within the timeout");
    case NotConnectedError:
        return tr("Not connected to the device");
    case ConnectionLostError:
        return tr("Connection closed before the answer was received");
    case InvalidRequestError:
        return tr("Invalid request parameters");
    case InvalidResponseError:
        return tr("Invalid answer from the device");
    case WriteError:
        return tr("The request could not be sent");
    }
    return tr("Unknown error");
}

QString SimpleModbusTcpClient::exceptionString(quint8 exceptionCode)
{
    switch (exceptionCode)
    {
    case NoException:
        return tr("No exception");
    case IllegalFunction:
        return tr("Illegal function");
    case IllegalDataAddress:
        return tr("Illegal data address");
    case IllegalDataValue:
        return tr("Illegal data value");
    case ServerDeviceFailure:
        return tr("Server device failure");
    case Acknowledge:
        return tr("Acknowledge");
    case ServerDeviceBusy:
        return tr("Server device busy");
    case NegativeAcknowledge:
        return tr("Negative acknowledge");
    case MemoryParityError:
        return tr("Memory parity error");
    case GatewayPathUnavailable:
        return tr("Gateway path unavailable");
    case GatewayTargetDeviceFailedToRespond:
        return tr("Gateway target device failed to respond");
    }
    return tr("Unknown exception 0x%1").arg(exceptionCode, 2, 16, QLatin1Char('0'));
}

// Internals :

quint16 SimpleModbusTcpClient::sendReadRequest(FunctionCode functionCode, quint16 startAddress, quint16 count, int maxCount)
{
    Request *request = new Request;
    request->functionCode = quint8(functionCode);
    request->address = startAddress;
    request->count = count;

    QByteArray pdu;
    pdu.append(char(functionCode));
    appendUInt16(pdu, startAddress);
    appendUInt16(pdu, count);
    return enqueueRequest(request, pdu, isValidRange(startAddress, count, maxCount));
}

quint16 SimpleModbusTcpClient::enqueueRequest(Request *request, const QByteArray &pdu, bool valid)
{
    request->transactionId = nextTransactionId();

    const QAbstractSocket::SocketState socketState = m_socket->state();
    if (!valid)
    {
        failRequest(request, InvalidRequestError);
    }
    else if (socketState != QAbstractSocket::ConnectedState
             && socketState != QAbstractSocket::HostLookupState
             && socketState != QAbstractSocket::ConnectingState)
    {
        failRequest(request, NotConnectedError);
    }
    else
    {
        // While connecting, the request waits in the queue until connected
        request->frame = buildFrame(request->transactionId, m_unitId, pdu);
        m_waitingRequests.enqueue(request);
        sendPendingRequests();
    }

    return request->transactionId;
}

// Skips 0 and the ids still in use. If the 65535 ids are all in use, the
// next one is reused anyway rather than looping forever.
quint16 SimpleModbusTcpClient::nextTransactionId()
{
    const bool allIdsInUse = m_usedTransactionIds.size() >= 0xFFFF;

    do
    {
        m_transactionId++;
    }
    while (m_transactionId == 0 || (!allIdsInUse && m_usedTransactionIds.contains(m_transactionId)));

    m_usedTransactionIds.insert(m_transactionId);
    return m_transactionId;
}

void SimpleModbusTcpClient::releaseRequest(Request *request)
{
    m_usedTransactionIds.remove(request->transactionId);
    delete request;
}

void SimpleModbusTcpClient::sendPendingRequests()
{
    if (m_socket->state() == QAbstractSocket::ConnectedState)
    {
        bool hasWritten = false;

        while (!m_waitingRequests.isEmpty()
               && (m_maxConcurrentRequests == 0 || m_sentRequests.size() < m_maxConcurrentRequests))
        {
            Request *request = m_waitingRequests.dequeue();

            if (m_socket->write(request->frame) != request->frame.size())
            {
                qCWarning(lcSimpleModbus, "Failed to write request %u: %s", uint(request->transactionId), qPrintable(m_socket->errorString()));
                failRequest(request, WriteError);
                continue;
            }

            request->sentTimer.start();
            m_sentRequests.insert(request->transactionId, request);
            hasWritten = true;
        }

        if (hasWritten)
            m_socket->flush();
    }

    updateTimeoutTimer();
}

// The failure is reported later from the event loop, so that requestFailed()
// is never emitted from inside a request method.
void SimpleModbusTcpClient::failRequest(Request *request, Error error, quint8 exceptionCode)
{
    request->error = error;
    request->exceptionCode = exceptionCode;
    m_failedRequests.enqueue(request);

    if (!m_failureReportScheduled)
    {
        m_failureReportScheduled = true;
        QTimer::singleShot(0, this, &SimpleModbusTcpClient::processFailedRequests);
    }
}

void SimpleModbusTcpClient::failAllRequests(Error error)
{
    const QList<Request *> sentRequests = m_sentRequests.values();
    m_sentRequests.clear();
    for (Request *request : sentRequests)
        failRequest(request, error);

    while (!m_waitingRequests.isEmpty())
        failRequest(m_waitingRequests.dequeue(), error);

    updateTimeoutTimer();
}

void SimpleModbusTcpClient::processFailedRequests()
{
    m_failureReportScheduled = false;

    // Failures added by the connected slots are reported on the next call
    int count = int(m_failedRequests.size());
    while (count > 0 && !m_failedRequests.isEmpty())
    {
        Request *request = m_failedRequests.dequeue();
        count--;

        emit requestFailed(request->transactionId, request->functionCode, request->address,
                           request->error, request->exceptionCode);
        releaseRequest(request);
    }
}

void SimpleModbusTcpClient::onReadyRead()
{
    m_buffer.append(m_socket->readAll());

    // The buffer is checked again after each frame: a connected slot may have
    // closed the connection, which clears it.
    while (m_buffer.size() >= MbapHeaderSize)
    {
        const quint16 protocolId = readUInt16(m_buffer, 2);
        const quint16 length = readUInt16(m_buffer, 4);

        if (protocolId != 0 || length < 2 || length > MaxMbapLength)
        {
            qCWarning(lcSimpleModbus, "Invalid MBAP header received, discarding %d bytes", int(m_buffer.size()));
            m_buffer.clear();
            break;
        }

        const int frameSize = 6 + length;
        if (m_buffer.size() < frameSize)
            break;

        const QByteArray frame = m_buffer.left(frameSize);
        m_buffer.remove(0, frameSize);
        processFrame(frame);
    }

    sendPendingRequests();
}

void SimpleModbusTcpClient::processFrame(const QByteArray &frame)
{
    const quint16 transactionId = readUInt16(frame, 0);

    Request *request = m_sentRequests.take(transactionId);
    if (request == nullptr)
    {
        qCWarning(lcSimpleModbus, "Ignoring answer to unknown transaction %u (answer after a timeout?)", uint(transactionId));
        return;
    }

    const QByteArray pdu = frame.mid(MbapHeaderSize);
    const quint8 functionCode = quint8(pdu.at(0));

    if (functionCode == (request->functionCode | 0x80))
    {
        if (pdu.size() == 2)
            failRequest(request, ExceptionError, quint8(pdu.at(1)));
        else
            failRequest(request, InvalidResponseError);
    }
    else if (functionCode != request->functionCode || !decodeResponse(request, pdu))
    {
        qCWarning(lcSimpleModbus, "Invalid answer to transaction %u: %s", uint(transactionId), frame.toHex().constData());
        failRequest(request, InvalidResponseError);
    }
    else
    {
        releaseRequest(request);
    }
}

// Emits the success signal if the answer is valid and matches the request.
bool SimpleModbusTcpClient::decodeResponse(const Request *request, const QByteArray &pdu)
{
    switch (request->functionCode)
    {
    case ReadCoils:
    case ReadDiscreteInputs:
    {
        const int byteCount = (request->count + 7) / 8;
        if (pdu.size() != 2 + byteCount || quint8(pdu.at(1)) != byteCount)
            return false;

        const QVector<bool> values = unpackBits(pdu, 2, request->count);
        if (request->functionCode == ReadCoils)
            emit coilsRead(request->transactionId, request->address, values);
        else
            emit discreteInputsRead(request->transactionId, request->address, values);
        return true;
    }

    case ReadHoldingRegisters:
    case ReadInputRegisters:
    {
        const int byteCount = 2 * request->count;
        if (pdu.size() != 2 + byteCount || quint8(pdu.at(1)) != byteCount)
            return false;

        QVector<quint16> values;
        values.reserve(request->count);
        for (int i = 0; i < request->count; i++)
            values.append(readUInt16(pdu, 2 + 2 * i));

        if (request->functionCode == ReadHoldingRegisters)
            emit holdingRegistersRead(request->transactionId, request->address, values);
        else
            emit inputRegistersRead(request->transactionId, request->address, values);
        return true;
    }

    case WriteSingleCoil:
    {
        const bool value = request->coils.first();
        if (pdu.size() != 5 || readUInt16(pdu, 1) != request->address || readUInt16(pdu, 3) != (value ? 0xFF00 : 0x0000))
            return false;

        emit singleCoilWritten(request->transactionId, request->address, value);
        return true;
    }

    case WriteSingleRegister:
    {
        const quint16 value = request->registers.first();
        if (pdu.size() != 5 || readUInt16(pdu, 1) != request->address || readUInt16(pdu, 3) != value)
            return false;

        emit singleRegisterWritten(request->transactionId, request->address, value);
        return true;
    }

    case WriteMultipleCoils:
    case WriteMultipleRegisters:
    {
        if (pdu.size() != 5 || readUInt16(pdu, 1) != request->address || readUInt16(pdu, 3) != request->count)
            return false;

        if (request->functionCode == WriteMultipleCoils)
            emit multipleCoilsWritten(request->transactionId, request->address, request->coils);
        else
            emit multipleRegistersWritten(request->transactionId, request->address, request->registers);
        return true;
    }
    }

    return false;
}

void SimpleModbusTcpClient::checkTimeouts()
{
    if (m_timeout > 0)
    {
        for (auto it = m_sentRequests.begin(); it != m_sentRequests.end();)
        {
            if (it.value()->sentTimer.hasExpired(m_timeout))
            {
                qCWarning(lcSimpleModbus, "Transaction %u timed out", uint(it.key()));
                failRequest(it.value(), TimeoutError);
                it = m_sentRequests.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    sendPendingRequests();
}

void SimpleModbusTcpClient::updateTimeoutTimer()
{
    if (m_timeout > 0 && !m_sentRequests.isEmpty())
    {
        const int interval = qBound(10, m_timeout / 10, 100);
        if (!m_timeoutTimer->isActive() || m_timeoutTimer->interval() != interval)
            m_timeoutTimer->start(interval);
    }
    else
    {
        m_timeoutTimer->stop();
    }
}

void SimpleModbusTcpClient::onStateChanged(QAbstractSocket::SocketState state)
{
    if (state == QAbstractSocket::ConnectedState)
    {
        m_buffer.clear();
        m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        sendPendingRequests();
    }
    else if (state == QAbstractSocket::UnconnectedState)
    {
        // A partial frame left in the buffer would corrupt the next connection
        m_buffer.clear();
        failAllRequests(ConnectionLostError);
    }

    emit stateChanged(state);
}

void SimpleModbusTcpClient::onSocketError(QAbstractSocket::SocketError error)
{
    emit socketError(error, m_socket->errorString());
}

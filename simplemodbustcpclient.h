#ifndef SIMPLEMODBUSTCPCLIENT_H
#define SIMPLEMODBUSTCPCLIENT_H

#include <QObject>
#include <QAbstractSocket>
#include <QByteArray>
#include <QMap>
#include <QQueue>
#include <QSet>
#include <QString>
#include <QVector>

class QTcpSocket;
class QTimer;

// Asynchronous Modbus TCP client.
//
// Every request method returns the transaction id of the request and, later
// on, exactly one signal is emitted for it: the matching "...Read" or
// "...Written" signal on success, or requestFailed() on failure. These
// signals are never emitted from inside the request method itself.
//
// Addresses are the protocol addresses, starting at 0 (holding register
// "40001" is address 0).
class SimpleModbusTcpClient : public QObject
{
    Q_OBJECT

public:
    enum FunctionCode {
        ReadCoils = 0x01,
        ReadDiscreteInputs = 0x02,
        ReadHoldingRegisters = 0x03,
        ReadInputRegisters = 0x04,
        WriteSingleCoil = 0x05,
        WriteSingleRegister = 0x06,
        WriteMultipleCoils = 0x0F,
        WriteMultipleRegisters = 0x10
    };
    Q_ENUM(FunctionCode)

    // Exception codes a device can answer with (see exceptionString()).
    enum ExceptionCode {
        NoException = 0x00,
        IllegalFunction = 0x01,
        IllegalDataAddress = 0x02,
        IllegalDataValue = 0x03,
        ServerDeviceFailure = 0x04,
        Acknowledge = 0x05,
        ServerDeviceBusy = 0x06,
        NegativeAcknowledge = 0x07,
        MemoryParityError = 0x08,
        GatewayPathUnavailable = 0x0A,
        GatewayTargetDeviceFailedToRespond = 0x0B
    };
    Q_ENUM(ExceptionCode)

    // Reason of a failed request (see requestFailed() and errorString()).
    enum Error {
        NoError,
        ExceptionError,         // The device answered with a Modbus exception
        TimeoutError,           // No answer within timeout()
        NotConnectedError,      // The request was issued while not connected
        ConnectionLostError,    // The connection was closed (or could not be opened) before the answer
        InvalidRequestError,    // Parameters outside of the Modbus limits
        InvalidResponseError,   // Malformed answer, or answer not matching the request
        WriteError              // The request could not be written to the socket
    };
    Q_ENUM(Error)

    explicit SimpleModbusTcpClient(QObject *parent = nullptr);
    explicit SimpleModbusTcpClient(const QString &host, quint16 port = 502, QObject *parent = nullptr);
    ~SimpleModbusTcpClient() override;

    // Connection
    void setHost(const QString &host);
    QString host() const;
    void setPort(quint16 port);
    quint16 port() const;

    // Does nothing if the client is not unconnected
    void connectToHost();
    void connectToHost(const QString &host, quint16 port = 502);
    void disconnectFromHost();
    QAbstractSocket::SocketState state() const;
    bool isConnected() const;

    // Underlying socket, for advanced settings only (socket options, proxy...).
    // Never read from or write to it directly.
    QTcpSocket *socket() const;

    // Unit identifier (slave id) put in the next requests. Default: 0.
    void setUnitId(quint8 unitId);
    quint8 unitId() const;

    // Maximum time to wait for an answer, in milliseconds. Default: 1000.
    // A value <= 0 disables the timeout.
    void setTimeout(int msec);
    int timeout() const;

    // Maximum number of requests sent without having received their answer,
    // the next ones are queued. Default: 0 (no limit). Use 1 for devices or
    // gateways handling a single transaction at a time.
    void setMaxConcurrentRequests(int count);
    int maxConcurrentRequests() const;

    // Number of requests whose signal has not been emitted yet.
    int pendingRequestCount() const;

    // Requests. Each one returns its transaction id (never 0).
    quint16 readCoils(quint16 startAddress, quint16 count);                                   // FC 0x01, count 1..2000
    quint16 readDiscreteInputs(quint16 startAddress, quint16 count);                          // FC 0x02, count 1..2000
    quint16 readHoldingRegisters(quint16 startAddress, quint16 count);                        // FC 0x03, count 1..125
    quint16 readInputRegisters(quint16 startAddress, quint16 count);                          // FC 0x04, count 1..125
    quint16 writeSingleCoil(quint16 address, bool value);                                     // FC 0x05
    quint16 writeSingleRegister(quint16 address, quint16 value);                              // FC 0x06
    quint16 writeMultipleCoils(quint16 startAddress, const QVector<bool> &values);            // FC 0x0F, 1..1968 values
    quint16 writeMultipleRegisters(quint16 startAddress, const QVector<quint16> &values);     // FC 0x10, 1..123 values

    static QString errorString(Error error);
    static QString exceptionString(quint8 exceptionCode);

signals:
    // Connection (forwarded from the socket)
    void connected();
    void disconnected();
    void stateChanged(QAbstractSocket::SocketState state);
    void socketError(QAbstractSocket::SocketError error, const QString &errorString);

    // Successful requests
    void coilsRead(quint16 transactionId, quint16 startAddress, const QVector<bool> &values);
    void discreteInputsRead(quint16 transactionId, quint16 startAddress, const QVector<bool> &values);
    void holdingRegistersRead(quint16 transactionId, quint16 startAddress, const QVector<quint16> &values);
    void inputRegistersRead(quint16 transactionId, quint16 startAddress, const QVector<quint16> &values);
    void singleCoilWritten(quint16 transactionId, quint16 address, bool value);
    void singleRegisterWritten(quint16 transactionId, quint16 address, quint16 value);
    void multipleCoilsWritten(quint16 transactionId, quint16 startAddress, const QVector<bool> &values);
    void multipleRegistersWritten(quint16 transactionId, quint16 startAddress, const QVector<quint16> &values);

    // Failed request. exceptionCode is only set when error is ExceptionError.
    void requestFailed(quint16 transactionId, quint8 functionCode, quint16 address,
                       SimpleModbusTcpClient::Error error, quint8 exceptionCode);

private:
    struct Request;

    quint16 sendReadRequest(FunctionCode functionCode, quint16 startAddress, quint16 count, int maxCount);
    quint16 enqueueRequest(Request *request, const QByteArray &pdu, bool valid);
    quint16 nextTransactionId();
    void releaseRequest(Request *request);
    void sendPendingRequests();
    void failRequest(Request *request, Error error, quint8 exceptionCode = NoException);
    void failAllRequests(Error error);
    void processFailedRequests();
    void processFrame(const QByteArray &frame);
    bool decodeResponse(const Request *request, const QByteArray &pdu);
    void checkTimeouts();
    void updateTimeoutTimer();

    void onReadyRead();
    void onStateChanged(QAbstractSocket::SocketState state);
    void onSocketError(QAbstractSocket::SocketError error);

    QTcpSocket *m_socket;
    QTimer *m_timeoutTimer;

    QString m_host;
    quint16 m_port;
    quint8 m_unitId;
    int m_timeout;
    int m_maxConcurrentRequests;
    quint16 m_transactionId;

    QByteArray m_buffer;
    QQueue<Request *> m_waitingRequests;     // Not sent yet
    QMap<quint16, Request *> m_sentRequests; // Waiting for their answer
    QQueue<Request *> m_failedRequests;      // Failure not reported yet
    QSet<quint16> m_usedTransactionIds;      // Ids of all the requests above
    bool m_failureReportScheduled;
};

#endif // SIMPLEMODBUSTCPCLIENT_H

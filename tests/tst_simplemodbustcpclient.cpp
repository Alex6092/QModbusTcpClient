#include "simplemodbustcpclient.h"

#include <QElapsedTimer>
#include <QPointer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

#include <functional>

namespace {

QByteArray hex(const char *data)
{
    return QByteArray::fromHex(data);
}

quint16 readUInt16(const QByteArray &data, int index)
{
    return quint16((quint8(data.at(index)) << 8) | quint8(data.at(index + 1)));
}

void appendUInt16(QByteArray &data, quint16 value)
{
    data.append(char(value >> 8));
    data.append(char(value & 0xFF));
}

// Builds an answer frame to the given request frame
QByteArray answerTo(const QByteArray &request, const QByteArray &pdu)
{
    QByteArray frame = request.left(4);
    appendUInt16(frame, quint16(pdu.size() + 1));
    frame.append(request.at(6));
    frame.append(pdu);
    return frame;
}

} // namespace

// Minimal Modbus TCP server: answers from its own memory unless a custom
// handler is set or autoReply is disabled.
class MockModbusServer : public QObject
{
    Q_OBJECT

public:
    static const int MemorySize = 2000; // Bigger addresses answer IllegalDataAddress

    QVector<bool> coils = QVector<bool>(MemorySize, false);
    QVector<bool> discreteInputs = QVector<bool>(MemorySize, false);
    QVector<quint16> holdingRegisters = QVector<quint16>(MemorySize, 0);
    QVector<quint16> inputRegisters = QVector<quint16>(MemorySize, 0);

    bool autoReply = true;
    std::function<QByteArray(const QByteArray &)> handler; // Returns the answer frame, empty for no answer
    QVector<QByteArray> requests;
    QPointer<QTcpSocket> connection;

    MockModbusServer()
    {
        connect(&server, &QTcpServer::newConnection, this, [this]() {
            connection = server.nextPendingConnection();
            connect(connection.data(), &QTcpSocket::readyRead, this, &MockModbusServer::onReadyRead);
        });
        server.listen(QHostAddress::LocalHost, 0);
    }

    quint16 port() const
    {
        return server.serverPort();
    }

    void send(const QByteArray &data)
    {
        connection->write(data);
        connection->flush();
    }

    QByteArray defaultAnswer(const QByteArray &request) const
    {
        const quint8 functionCode = quint8(request.at(7));
        const quint16 address = readUInt16(request, 8);
        const quint16 value = readUInt16(request, 10);
        const int count = (functionCode == 0x05 || functionCode == 0x06) ? 1 : value;
        QByteArray pdu(1, char(functionCode));

        if (address + count > MemorySize)
            return answerTo(request, QByteArray(1, char(functionCode | 0x80)) + char(0x02));

        switch (functionCode)
        {
        case 0x01:
        case 0x02:
        {
            const QVector<bool> &bits = functionCode == 0x01 ? coils : discreteInputs;
            QByteArray bytes((count + 7) / 8, '\0');
            for (int i = 0; i < count; i++)
            {
                if (bits.at(address + i))
                    bytes[i / 8] = char(bytes.at(i / 8) | (1 << (i % 8)));
            }
            pdu.append(char(bytes.size()));
            pdu.append(bytes);
            break;
        }
        case 0x03:
        case 0x04:
        {
            const QVector<quint16> &registers = functionCode == 0x03 ? holdingRegisters : inputRegisters;
            pdu.append(char(2 * count));
            for (int i = 0; i < count; i++)
                appendUInt16(pdu, registers.at(address + i));
            break;
        }
        case 0x05:
        case 0x06:
        case 0x0F:
        case 0x10:
            pdu.append(request.mid(8, 4));
            break;
        default:
            return answerTo(request, QByteArray(1, char(functionCode | 0x80)) + char(0x01));
        }

        return answerTo(request, pdu);
    }

    void applyWrite(const QByteArray &request)
    {
        const quint8 functionCode = quint8(request.at(7));
        const quint16 address = readUInt16(request, 8);
        const quint16 value = readUInt16(request, 10);

        if (functionCode == 0x05 && address < MemorySize)
            coils[address] = value == 0xFF00;
        else if (functionCode == 0x06 && address < MemorySize)
            holdingRegisters[address] = value;
        else if (functionCode == 0x0F && address + value <= MemorySize)
        {
            for (int i = 0; i < value; i++)
                coils[address + i] = (quint8(request.at(13 + i / 8)) >> (i % 8)) & 0x01;
        }
        else if (functionCode == 0x10 && address + value <= MemorySize)
        {
            for (int i = 0; i < value; i++)
                holdingRegisters[address + i] = readUInt16(request, 13 + 2 * i);
        }
    }

private:
    void onReadyRead()
    {
        buffer.append(connection->readAll());

        while (buffer.size() >= 6 && buffer.size() >= 6 + readUInt16(buffer, 4))
        {
            const QByteArray request = buffer.left(6 + readUInt16(buffer, 4));
            buffer.remove(0, request.size());
            requests.append(request);

            if (autoReply)
            {
                const QByteArray answer = handler ? handler(request) : defaultAnswer(request);
                if (!handler)
                    applyWrite(request);
                if (!answer.isEmpty())
                    send(answer);
            }
        }
    }

    QTcpServer server;
    QByteArray buffer;
};

class tst_SimpleModbusTcpClient : public QObject
{
    Q_OBJECT

private:
    MockModbusServer *server = nullptr;
    SimpleModbusTcpClient *client = nullptr;

    static SimpleModbusTcpClient::Error errorOf(const QList<QVariant> &arguments)
    {
        return arguments.at(3).value<SimpleModbusTcpClient::Error>();
    }

private slots:
    void init()
    {
        server = new MockModbusServer;
        client = new SimpleModbusTcpClient(QStringLiteral("127.0.0.1"), server->port());
        client->connectToHost();
        QTRY_VERIFY(client->isConnected());
        QTRY_VERIFY(!server->connection.isNull());
    }

    void cleanup()
    {
        delete client;
        client = nullptr;
        delete server;
        server = nullptr;
    }

    void requestFrames()
    {
        client->setUnitId(0x11);

        const quint16 ids[] = {
            client->readCoils(19, 19),
            client->readDiscreteInputs(196, 22),
            client->readHoldingRegisters(107, 3),
            client->readInputRegisters(8, 1),
            client->writeSingleCoil(172, true),
            client->writeSingleRegister(1, 3),
            client->writeMultipleCoils(19, {true, false, true, true, false, false, true, true, true, false}),
            client->writeMultipleRegisters(1, {0x000A, 0x0102})
        };
        // Examples of the Modbus application protocol specification
        const QByteArray expected[] = {
            hex("0000000611" "0100130013"),
            hex("0000000611" "0200C40016"),
            hex("0000000611" "03006B0003"),
            hex("0000000611" "0400080001"),
            hex("0000000611" "0500ACFF00"),
            hex("0000000611" "0600010003"),
            hex("0000000911" "0F0013000A02CD01"),
            hex("0000000B11" "100001000204000A0102")
        };

        QTRY_COMPARE(server->requests.size(), 8);
        for (int i = 0; i < 8; i++)
        {
            QCOMPARE(readUInt16(server->requests.at(i), 0), ids[i]);
            QCOMPARE(server->requests.at(i).mid(2).toHex(), expected[i].toHex());
        }
    }

    void unitIdIsTakenAtRequestTime()
    {
        client->setUnitId(5);
        client->readHoldingRegisters(0, 1);
        client->setUnitId(0xFF);
        client->readHoldingRegisters(0, 1);

        QTRY_COMPARE(server->requests.size(), 2);
        QCOMPARE(quint8(server->requests.at(0).at(6)), quint8(5));
        QCOMPARE(quint8(server->requests.at(1).at(6)), quint8(0xFF));
        QCOMPARE(client->unitId(), quint8(0xFF));
    }

    void decodeSpecificationAnswers()
    {
        server->handler = [](const QByteArray &request) {
            switch (request.at(7))
            {
            case 0x01: return answerTo(request, hex("0103CD6B05"));
            case 0x02: return answerTo(request, hex("0203ACDB35"));
            case 0x03: return answerTo(request, hex("0306022B00000064"));
            case 0x04: return answerTo(request, hex("0402000A"));
            }
            return QByteArray();
        };
        QSignalSpy coilsSpy(client, &SimpleModbusTcpClient::coilsRead);
        QSignalSpy inputsSpy(client, &SimpleModbusTcpClient::discreteInputsRead);
        QSignalSpy holdingSpy(client, &SimpleModbusTcpClient::holdingRegistersRead);
        QSignalSpy inputRegistersSpy(client, &SimpleModbusTcpClient::inputRegistersRead);

        const quint16 coilsId = client->readCoils(19, 19);
        const quint16 inputsId = client->readDiscreteInputs(196, 22);
        const quint16 holdingId = client->readHoldingRegisters(107, 3);
        const quint16 inputRegistersId = client->readInputRegisters(8, 1);

        QTRY_COMPARE(inputRegistersSpy.count(), 1);
        QCOMPARE(coilsSpy.count(), 1);
        QCOMPARE(inputsSpy.count(), 1);
        QCOMPARE(holdingSpy.count(), 1);

        const QVector<bool> expectedCoils = {true, false, true, true, false, false, true, true,
                                             true, true, false, true, false, true, true, false,
                                             true, false, true};
        QCOMPARE(coilsSpy.at(0).at(0).value<quint16>(), coilsId);
        QCOMPARE(coilsSpy.at(0).at(1).value<quint16>(), quint16(19));
        QCOMPARE(coilsSpy.at(0).at(2).value<QVector<bool> >(), expectedCoils);

        const QVector<bool> inputs = inputsSpy.at(0).at(2).value<QVector<bool> >();
        QCOMPARE(inputsSpy.at(0).at(0).value<quint16>(), inputsId);
        QCOMPARE(inputs.size(), 22);
        QCOMPARE(inputs.mid(0, 8), QVector<bool>({false, false, true, true, false, true, false, true}));
        QCOMPARE(inputs.mid(16), QVector<bool>({true, false, true, false, true, true}));

        QCOMPARE(holdingSpy.at(0).at(0).value<quint16>(), holdingId);
        QCOMPARE(holdingSpy.at(0).at(1).value<quint16>(), quint16(107));
        QCOMPARE(holdingSpy.at(0).at(2).value<QVector<quint16> >(), QVector<quint16>({0x022B, 0x0000, 0x0064}));

        QCOMPARE(inputRegistersSpy.at(0).at(0).value<quint16>(), inputRegistersId);
        QCOMPARE(inputRegistersSpy.at(0).at(2).value<QVector<quint16> >(), QVector<quint16>({0x000A}));
    }

    // Regression: the frame length was read from a signed char, so lengths
    // with a low byte >= 0x80 broke the parsing (endless loop).
    void readMaximumNumberOfRegisters()
    {
        for (int i = 0; i < 125; i++)
            server->holdingRegisters[i] = quint16(0x8000 + i * 3);

        QSignalSpy spy(client, &SimpleModbusTcpClient::holdingRegistersRead);
        client->readHoldingRegisters(0, 125);
        client->readHoldingRegisters(0, 64);

        QTRY_COMPARE(spy.count(), 2);
        QCOMPARE(spy.at(0).at(2).value<QVector<quint16> >(), server->holdingRegisters.mid(0, 125));
        QCOMPARE(spy.at(1).at(2).value<QVector<quint16> >(), server->holdingRegisters.mid(0, 64));
    }

    void readMaximumNumberOfBits()
    {
        for (int i = 0; i < 2000; i++)
            server->discreteInputs[i] = (i % 3) == 0;

        QSignalSpy spy(client, &SimpleModbusTcpClient::discreteInputsRead);
        client->readDiscreteInputs(0, 2000);

        QTRY_COMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(2).value<QVector<bool> >(), server->discreteInputs.mid(0, 2000));
    }

    void writeAndReadBack()
    {
        QSignalSpy coilSpy(client, &SimpleModbusTcpClient::singleCoilWritten);
        QSignalSpy registerSpy(client, &SimpleModbusTcpClient::singleRegisterWritten);
        QSignalSpy coilsSpy(client, &SimpleModbusTcpClient::multipleCoilsWritten);
        QSignalSpy registersSpy(client, &SimpleModbusTcpClient::multipleRegistersWritten);
        QSignalSpy coilsReadSpy(client, &SimpleModbusTcpClient::coilsRead);
        QSignalSpy registersReadSpy(client, &SimpleModbusTcpClient::holdingRegistersRead);
        QSignalSpy failedSpy(client, &SimpleModbusTcpClient::requestFailed);

        const QVector<bool> coils = {true, true, false, true, false, false, false, true, true, false, true};
        QVector<quint16> registers;
        for (int i = 0; i < 123; i++)
            registers.append(quint16(i * 517));

        const quint16 coilId = client->writeSingleCoil(3, true);
        const quint16 registerId = client->writeSingleRegister(4, 0xBEEF);
        const quint16 coilsId = client->writeMultipleCoils(10, coils);
        const quint16 registersId = client->writeMultipleRegisters(100, registers);
        client->readCoils(10, quint16(coils.size()));
        client->readHoldingRegisters(100, 123);

        QTRY_COMPARE(registersReadSpy.count(), 1);
        QCOMPARE(failedSpy.count(), 0);

        QCOMPARE(coilSpy.count(), 1);
        QCOMPARE(coilSpy.at(0).at(0).value<quint16>(), coilId);
        QCOMPARE(coilSpy.at(0).at(1).value<quint16>(), quint16(3));
        QCOMPARE(coilSpy.at(0).at(2).toBool(), true);
        QCOMPARE(server->coils.at(3), true);

        QCOMPARE(registerSpy.count(), 1);
        QCOMPARE(registerSpy.at(0).at(0).value<quint16>(), registerId);
        QCOMPARE(registerSpy.at(0).at(2).value<quint16>(), quint16(0xBEEF));
        QCOMPARE(server->holdingRegisters.at(4), quint16(0xBEEF));

        QCOMPARE(coilsSpy.count(), 1);
        QCOMPARE(coilsSpy.at(0).at(0).value<quint16>(), coilsId);
        QCOMPARE(coilsSpy.at(0).at(1).value<quint16>(), quint16(10));
        QCOMPARE(coilsSpy.at(0).at(2).value<QVector<bool> >(), coils);
        QCOMPARE(coilsReadSpy.at(0).at(2).value<QVector<bool> >(), coils);

        // Regression: only 12 bytes were sent and the byte count was wrong
        const QByteArray fc16Request = server->requests.at(3);
        QCOMPARE(fc16Request.size(), 7 + 6 + 2 * 123);
        QCOMPARE(quint8(fc16Request.at(12)), quint8(2 * 123));
        QCOMPARE(registersSpy.count(), 1);
        QCOMPARE(registersSpy.at(0).at(0).value<quint16>(), registersId);
        QCOMPARE(registersSpy.at(0).at(2).value<QVector<quint16> >(), registers);
        QCOMPARE(registersReadSpy.at(0).at(2).value<QVector<quint16> >(), registers);
    }

    void exceptionAnswers()
    {
        QSignalSpy failedSpy(client, &SimpleModbusTcpClient::requestFailed);
        QSignalSpy readSpy(client, &SimpleModbusTcpClient::holdingRegistersRead);
        QSignalSpy writeSpy(client, &SimpleModbusTcpClient::singleRegisterWritten);

        const quint16 readId = client->readHoldingRegisters(2000, 2);
        const quint16 writeId = client->writeSingleRegister(2500, 1);

        QTRY_COMPARE(failedSpy.count(), 2);
        QCOMPARE(readSpy.count(), 0);
        QCOMPARE(writeSpy.count(), 0);

        QCOMPARE(failedSpy.at(0).at(0).value<quint16>(), readId);
        QCOMPARE(failedSpy.at(0).at(1).value<quint8>(), quint8(SimpleModbusTcpClient::ReadHoldingRegisters));
        QCOMPARE(failedSpy.at(0).at(2).value<quint16>(), quint16(2000));
        QCOMPARE(errorOf(failedSpy.at(0)), SimpleModbusTcpClient::ExceptionError);
        QCOMPARE(failedSpy.at(0).at(4).value<quint8>(), quint8(SimpleModbusTcpClient::IllegalDataAddress));

        QCOMPARE(failedSpy.at(1).at(0).value<quint16>(), writeId);
        QCOMPARE(failedSpy.at(1).at(1).value<quint8>(), quint8(SimpleModbusTcpClient::WriteSingleRegister));
        QCOMPARE(errorOf(failedSpy.at(1)), SimpleModbusTcpClient::ExceptionError);
        QCOMPARE(failedSpy.at(1).at(4).value<quint8>(), quint8(0x02));
    }

    void customExceptionCode()
    {
        server->handler = [](const QByteArray &request) { return answerTo(request, hex("8142")); };
        QSignalSpy failedSpy(client, &SimpleModbusTcpClient::requestFailed);

        client->readCoils(0, 1);

        QTRY_COMPARE(failedSpy.count(), 1);
        QCOMPARE(errorOf(failedSpy.at(0)), SimpleModbusTcpClient::ExceptionError);
        QCOMPARE(failedSpy.at(0).at(4).value<quint8>(), quint8(0x42));
        QCOMPARE(SimpleModbusTcpClient::exceptionString(0x42), QStringLiteral("Unknown exception 0x42"));
    }

    void invalidAnswers_data()
    {
        QTest::addColumn<int>("functionCode");
        QTest::addColumn<QByteArray>("answerPdu");

        QTest::newRow("wrong byte count") << 3 << hex("0304000100020003");
        QTest::newRow("missing registers") << 3 << hex("030600010002");
        QTest::newRow("too many registers") << 3 << hex("030600010002000300040005");
        QTest::newRow("wrong function code") << 3 << hex("0406000100020003");
        QTest::newRow("empty exception") << 3 << hex("83");
        QTest::newRow("wrong coil byte count") << 1 << hex("01020000");
        QTest::newRow("wrong single coil echo") << 5 << hex("0500AC0000");
        QTest::newRow("wrong single register echo") << 6 << hex("0600010004");
        QTest::newRow("wrong address echo") << 16 << hex("1000020002");
        QTest::newRow("wrong quantity echo") << 15 << hex("0F0013000B");
        QTest::newRow("truncated echo") << 6 << hex("060001");
    }

    void invalidAnswers()
    {
        QFETCH(int, functionCode);
        QFETCH(QByteArray, answerPdu);

        server->handler = [answerPdu](const QByteArray &request) { return answerTo(request, answerPdu); };
        QSignalSpy failedSpy(client, &SimpleModbusTcpClient::requestFailed);

        quint16 id = 0;
        switch (functionCode)
        {
        case 1: id = client->readCoils(19, 19); break;
        case 3: id = client->readHoldingRegisters(107, 3); break;
        case 5: id = client->writeSingleCoil(172, true); break;
        case 6: id = client->writeSingleRegister(1, 3); break;
        case 15: id = client->writeMultipleCoils(19, QVector<bool>(10, true)); break;
        case 16: id = client->writeMultipleRegisters(1, {1, 2}); break;
        }

        QTRY_COMPARE(failedSpy.count(), 1);
        QCOMPARE(failedSpy.at(0).at(0).value<quint16>(), id);
        QCOMPARE(failedSpy.at(0).at(1).value<quint8>(), quint8(functionCode));
        QCOMPARE(errorOf(failedSpy.at(0)), SimpleModbusTcpClient::InvalidResponseError);

        // The connection is still usable
        server->handler = nullptr;
        QSignalSpy readSpy(client, &SimpleModbusTcpClient::holdingRegistersRead);
        client->readHoldingRegisters(0, 2);
        QTRY_COMPARE(readSpy.count(), 1);
    }

    void invalidRequests()
    {
        QSignalSpy failedSpy(client, &SimpleModbusTcpClient::requestFailed);

        const QList<std::function<quint16()> > requests = {
            [this]() { return client->readCoils(0, 0); },
            [this]() { return client->readCoils(0, 2001); },
            [this]() { return client->readDiscreteInputs(0, 2001); },
            [this]() { return client->readHoldingRegisters(0, 0); },
            [this]() { return client->readHoldingRegisters(0, 126); },
            [this]() { return client->readInputRegisters(0, 126); },
            [this]() { return client->readHoldingRegisters(65535, 2); },
            [this]() { return client->writeMultipleCoils(0, QVector<bool>()); }, // Regression: buffer overflow
            [this]() { return client->writeMultipleCoils(0, QVector<bool>(1969, true)); },
            [this]() { return client->writeMultipleRegisters(0, QVector<quint16>()); },
            [this]() { return client->writeMultipleRegisters(0, QVector<quint16>(124, 1)); },
            [this]() { return client->writeMultipleRegisters(65500, QVector<quint16>(100, 1)); }
        };

        QVector<quint16> ids;
        for (const auto &request : requests)
        {
            ids.append(request());
            QVERIFY(ids.last() != 0);
        }

        // Never reported from inside the request call
        QCOMPARE(failedSpy.count(), 0);
        QTRY_COMPARE(failedSpy.count(), requests.size());

        for (int i = 0; i < requests.size(); i++)
        {
            QCOMPARE(failedSpy.at(i).at(0).value<quint16>(), ids.at(i));
            QCOMPARE(errorOf(failedSpy.at(i)), SimpleModbusTcpClient::InvalidRequestError);
        }

        QTest::qWait(50);
        QCOMPARE(server->requests.size(), 0);
        QCOMPARE(client->pendingRequestCount(), 0);
    }

    void limitsAreAccepted()
    {
        QSignalSpy failedSpy(client, &SimpleModbusTcpClient::requestFailed);

        client->readCoils(65535, 1);
        client->readDiscreteInputs(0, 2000);
        client->readInputRegisters(0, 125);
        client->writeMultipleCoils(0, QVector<bool>(1968, true));
        client->writeMultipleRegisters(65413, QVector<quint16>(123, 1));

        // Sent to the server, which answers IllegalDataAddress beyond its memory
        QTRY_COMPARE(server->requests.size(), 5);
        QTRY_COMPARE(client->pendingRequestCount(), 0);
        QCOMPARE(failedSpy.count(), 2);
        for (const QList<QVariant> &arguments : failedSpy)
            QCOMPARE(errorOf(arguments), SimpleModbusTcpClient::ExceptionError);
    }

    void notConnected()
    {
        SimpleModbusTcpClient offlineClient(QStringLiteral("127.0.0.1"), server->port());
        QSignalSpy failedSpy(&offlineClient, &SimpleModbusTcpClient::requestFailed);

        const quint16 id = offlineClient.writeSingleCoil(1, true);

        QVERIFY(id != 0);
        QCOMPARE(failedSpy.count(), 0);
        QTRY_COMPARE(failedSpy.count(), 1);
        QCOMPARE(failedSpy.at(0).at(0).value<quint16>(), id);
        QCOMPARE(errorOf(failedSpy.at(0)), SimpleModbusTcpClient::NotConnectedError);
    }

    void requestWhileConnecting()
    {
        SimpleModbusTcpClient newClient;
        QSignalSpy readSpy(&newClient, &SimpleModbusTcpClient::holdingRegistersRead);
        QSignalSpy connectedSpy(&newClient, &SimpleModbusTcpClient::connected);

        newClient.connectToHost(QStringLiteral("127.0.0.1"), server->port());
        newClient.readHoldingRegisters(0, 1);

        QTRY_COMPARE(readSpy.count(), 1);
        QCOMPARE(connectedSpy.count(), 1);
        QCOMPARE(newClient.host(), QStringLiteral("127.0.0.1"));
        QCOMPARE(newClient.port(), server->port());
    }

    void connectionRefused()
    {
        QTcpServer closedServer;
        QVERIFY(closedServer.listen(QHostAddress::LocalHost, 0));
        const quint16 closedPort = closedServer.serverPort();
        closedServer.close();

        SimpleModbusTcpClient newClient(QStringLiteral("127.0.0.1"), closedPort);
        QSignalSpy failedSpy(&newClient, &SimpleModbusTcpClient::requestFailed);
        QSignalSpy socketErrorSpy(&newClient, &SimpleModbusTcpClient::socketError);

        newClient.connectToHost();
        newClient.readHoldingRegisters(0, 1);

        QTRY_COMPARE(failedSpy.count(), 1);
        QCOMPARE(errorOf(failedSpy.at(0)), SimpleModbusTcpClient::ConnectionLostError);
        QVERIFY(socketErrorSpy.count() >= 1);
        QCOMPARE(socketErrorSpy.at(0).at(0).value<QAbstractSocket::SocketError>(), QAbstractSocket::ConnectionRefusedError);
    }

    void timeoutAndLateAnswer()
    {
        server->autoReply = false;
        client->setTimeout(100);
        QCOMPARE(client->timeout(), 100);
        QSignalSpy failedSpy(client, &SimpleModbusTcpClient::requestFailed);
        QSignalSpy readSpy(client, &SimpleModbusTcpClient::holdingRegistersRead);

        QElapsedTimer timer;
        timer.start();
        const quint16 id = client->readHoldingRegisters(0, 1);

        QTRY_COMPARE(failedSpy.count(), 1);
        QVERIFY(timer.elapsed() >= 100);
        QCOMPARE(failedSpy.at(0).at(0).value<quint16>(), id);
        QCOMPARE(errorOf(failedSpy.at(0)), SimpleModbusTcpClient::TimeoutError);
        QCOMPARE(client->pendingRequestCount(), 0);

        // The late answer is ignored
        server->send(server->defaultAnswer(server->requests.at(0)));
        QTest::qWait(50);
        QCOMPARE(readSpy.count(), 0);
        QCOMPARE(failedSpy.count(), 1);

        server->autoReply = true;
        client->readHoldingRegisters(0, 1);
        QTRY_COMPARE(readSpy.count(), 1);
    }

    void timeoutDisabled()
    {
        server->autoReply = false;
        client->setTimeout(0);
        QSignalSpy failedSpy(client, &SimpleModbusTcpClient::requestFailed);
        QSignalSpy readSpy(client, &SimpleModbusTcpClient::holdingRegistersRead);

        client->readHoldingRegisters(0, 1);
        QTest::qWait(300);
        QCOMPARE(failedSpy.count(), 0);
        QCOMPARE(client->pendingRequestCount(), 1);

        server->send(server->defaultAnswer(server->requests.at(0)));
        QTRY_COMPARE(readSpy.count(), 1);
    }

    void fragmentedAnswer()
    {
        server->autoReply = false;
        QSignalSpy readSpy(client, &SimpleModbusTcpClient::holdingRegistersRead);
        server->holdingRegisters[0] = 0x1234;
        server->holdingRegisters[1] = 0xABCD;

        client->readHoldingRegisters(0, 2);
        QTRY_COMPARE(server->requests.size(), 1);

        const QByteArray answer = server->defaultAnswer(server->requests.at(0));
        for (char byte : answer)
        {
            server->send(QByteArray(1, byte));
            QTest::qWait(2);
        }

        QTRY_COMPARE(readSpy.count(), 1);
        QCOMPARE(readSpy.at(0).at(2).value<QVector<quint16> >(), QVector<quint16>({0x1234, 0xABCD}));
    }

    void groupedAndUnorderedAnswers()
    {
        server->autoReply = false;
        QSignalSpy readSpy(client, &SimpleModbusTcpClient::holdingRegistersRead);
        QSignalSpy coilsSpy(client, &SimpleModbusTcpClient::coilsRead);
        server->holdingRegisters[7] = 77;
        server->coils[3] = true;

        const quint16 readId = client->readHoldingRegisters(7, 1);
        const quint16 coilsId = client->readCoils(3, 1);
        QTRY_COMPARE(server->requests.size(), 2);

        server->send(server->defaultAnswer(server->requests.at(1)) + server->defaultAnswer(server->requests.at(0)));

        QTRY_COMPARE(readSpy.count(), 1);
        QCOMPARE(coilsSpy.count(), 1);
        QCOMPARE(readSpy.at(0).at(0).value<quint16>(), readId);
        QCOMPARE(readSpy.at(0).at(2).value<QVector<quint16> >(), QVector<quint16>({77}));
        QCOMPARE(coilsSpy.at(0).at(0).value<quint16>(), coilsId);
        QCOMPARE(coilsSpy.at(0).at(2).value<QVector<bool> >(), QVector<bool>({true}));
    }

    void invalidHeaderIsDiscarded()
    {
        server->autoReply = false;
        client->setTimeout(200);
        QSignalSpy failedSpy(client, &SimpleModbusTcpClient::requestFailed);
        QSignalSpy readSpy(client, &SimpleModbusTcpClient::holdingRegistersRead);

        client->readHoldingRegisters(0, 1);
        QTRY_COMPARE(server->requests.size(), 1);
        server->send(hex("0001ABCD000603020000"));

        QTRY_COMPARE(failedSpy.count(), 1);
        QCOMPARE(errorOf(failedSpy.at(0)), SimpleModbusTcpClient::TimeoutError);

        server->autoReply = true;
        client->readHoldingRegisters(0, 1);
        QTRY_COMPARE(readSpy.count(), 1);
    }

    void unknownTransactionIsIgnored()
    {
        QSignalSpy readSpy(client, &SimpleModbusTcpClient::holdingRegistersRead);
        QSignalSpy failedSpy(client, &SimpleModbusTcpClient::requestFailed);

        server->send(hex("BEEF00000005000302002A"));
        client->readHoldingRegisters(0, 1);

        QTRY_COMPARE(readSpy.count(), 1);
        QCOMPARE(failedSpy.count(), 0);
    }

    void connectionLost()
    {
        server->autoReply = false;
        client->setMaxConcurrentRequests(1);
        QSignalSpy failedSpy(client, &SimpleModbusTcpClient::requestFailed);
        QSignalSpy disconnectedSpy(client, &SimpleModbusTcpClient::disconnected);

        const quint16 sentId = client->readHoldingRegisters(0, 1);
        const quint16 waitingId = client->readHoldingRegisters(1, 1);
        QTRY_COMPARE(server->requests.size(), 1);
        QCOMPARE(client->pendingRequestCount(), 2);

        server->connection->abort();

        QTRY_COMPARE(failedSpy.count(), 2);
        QCOMPARE(disconnectedSpy.count(), 1);
        QCOMPARE(failedSpy.at(0).at(0).value<quint16>(), sentId);
        QCOMPARE(failedSpy.at(1).at(0).value<quint16>(), waitingId);
        QCOMPARE(errorOf(failedSpy.at(0)), SimpleModbusTcpClient::ConnectionLostError);
        QCOMPARE(errorOf(failedSpy.at(1)), SimpleModbusTcpClient::ConnectionLostError);
        QCOMPARE(client->pendingRequestCount(), 0);
        QVERIFY(!client->isConnected());

        // Reconnection
        client->connectToHost();
        QTRY_VERIFY(client->isConnected());
        server->autoReply = true;
        QSignalSpy readSpy(client, &SimpleModbusTcpClient::holdingRegistersRead);
        client->readHoldingRegisters(0, 1);
        QTRY_COMPARE(readSpy.count(), 1);
    }

    void disconnectFromSlot()
    {
        server->autoReply = false;
        QSignalSpy readSpy(client, &SimpleModbusTcpClient::holdingRegistersRead);
        QSignalSpy failedSpy(client, &SimpleModbusTcpClient::requestFailed);
        connect(client, &SimpleModbusTcpClient::holdingRegistersRead, client, &SimpleModbusTcpClient::disconnectFromHost);

        client->readHoldingRegisters(0, 1);
        const quint16 secondId = client->readHoldingRegisters(1, 1);
        QTRY_COMPARE(server->requests.size(), 2);

        server->send(server->defaultAnswer(server->requests.at(0)) + server->defaultAnswer(server->requests.at(1)));

        QTRY_COMPARE(failedSpy.count(), 1);
        QCOMPARE(readSpy.count(), 1);
        QCOMPARE(failedSpy.at(0).at(0).value<quint16>(), secondId);
        QCOMPARE(errorOf(failedSpy.at(0)), SimpleModbusTcpClient::ConnectionLostError);
    }

    void requestFromSlot()
    {
        QSignalSpy readSpy(client, &SimpleModbusTcpClient::holdingRegistersRead);
        int remaining = 5;
        connect(client, &SimpleModbusTcpClient::holdingRegistersRead, this, [this, &remaining]() {
            if (--remaining > 0)
                client->readHoldingRegisters(0, 1);
        });

        client->readHoldingRegisters(0, 1);
        QTRY_COMPARE(readSpy.count(), 5);
    }

    void maxConcurrentRequests()
    {
        server->autoReply = false;
        client->setMaxConcurrentRequests(1);
        QCOMPARE(client->maxConcurrentRequests(), 1);
        QSignalSpy readSpy(client, &SimpleModbusTcpClient::holdingRegistersRead);

        for (int i = 0; i < 3; i++)
            client->readHoldingRegisters(quint16(i), 1);

        for (int i = 0; i < 3; i++)
        {
            QTRY_COMPARE(server->requests.size(), i + 1);
            QTest::qWait(30);
            QCOMPARE(server->requests.size(), i + 1);
            QCOMPARE(readUInt16(server->requests.at(i), 8), quint16(i));
            server->send(server->defaultAnswer(server->requests.at(i)));
        }

        QTRY_COMPARE(readSpy.count(), 3);

        // Removing the limit sends the queued requests
        for (int i = 0; i < 3; i++)
            client->readHoldingRegisters(0, 1);
        QTest::qWait(30);
        QCOMPARE(server->requests.size(), 4);
        client->setMaxConcurrentRequests(0);
        QTRY_COMPARE(server->requests.size(), 6);
    }

    void transactionIdsWrapAroundWithoutZero()
    {
        SimpleModbusTcpClient offlineClient;
        QSignalSpy failedSpy(&offlineClient, &SimpleModbusTcpClient::requestFailed);

        quint16 previous = 0;
        for (int batch = 0; batch < 140; batch++)
        {
            for (int i = 0; i < 500; i++)
            {
                const quint16 id = offlineClient.readCoils(0, 1);
                QVERIFY(id != 0);
                QVERIFY(id == quint16(previous + 1) || (previous == 0xFFFF && id == 1));
                previous = id;
            }
            for (int i = 0; i < 100 && offlineClient.pendingRequestCount() > 0; i++)
                QCoreApplication::processEvents();
            QCOMPARE(offlineClient.pendingRequestCount(), 0);
        }
        QCOMPARE(failedSpy.count(), 140 * 500);
    }

    // More pending requests than transaction ids: must neither hang nor leak
    void floodWhileOffline()
    {
        SimpleModbusTcpClient offlineClient;
        QSignalSpy failedSpy(&offlineClient, &SimpleModbusTcpClient::requestFailed);

        for (int i = 0; i < 70000; i++)
        {
            if (offlineClient.writeSingleRegister(0, 1) == 0)
                QFAIL("Transaction id 0");
        }
        QCOMPARE(offlineClient.pendingRequestCount(), 70000);

        QTRY_COMPARE(failedSpy.count(), 70000);
        QCOMPARE(offlineClient.pendingRequestCount(), 0);
    }

    void destroyWithPendingRequests()
    {
        server->autoReply = false;
        int signalCount = 0;
        auto count = [&signalCount]() { signalCount++; };
        connect(client, &SimpleModbusTcpClient::requestFailed, this, count);
        connect(client, &SimpleModbusTcpClient::disconnected, this, count);
        connect(client, &SimpleModbusTcpClient::stateChanged, this, count);

        client->setMaxConcurrentRequests(1);
        client->readHoldingRegisters(0, 1);
        client->readHoldingRegisters(0, 1);
        QTRY_COMPARE(server->requests.size(), 1);
        client->readHoldingRegisters(0, 0); // Failure not reported yet
        QCOMPARE(client->pendingRequestCount(), 3);

        delete client;
        client = nullptr;
        QTest::qWait(50);
        QCOMPARE(signalCount, 0);
    }

    void strings()
    {
        QCOMPARE(SimpleModbusTcpClient::exceptionString(SimpleModbusTcpClient::IllegalDataAddress), QStringLiteral("Illegal data address"));
        QCOMPARE(SimpleModbusTcpClient::errorString(SimpleModbusTcpClient::TimeoutError), QStringLiteral("No answer from the device within the timeout"));
    }
};

QTEST_MAIN(tst_SimpleModbusTcpClient)

#include "tst_simplemodbustcpclient.moc"

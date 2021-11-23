#ifndef QModbusTcpClient_H
#define QModbusTcpClient_H

#include <QObject>
#include <QTcpSocket>
#include <QMap>
#include <QVector>

class QModbusTcpClient;

class ModbusRequest
{
    QModbusTcpClient * client;
    quint16 transactionId;

protected:
    QModbusTcpClient * getClient() {
        return client;
    }

public:
    ModbusRequest(QModbusTcpClient * client, quint16 transactionId);
    virtual ~ModbusRequest();
    quint16 getTransactionId();
    virtual quint8 getFunctionCode() = 0;
    virtual void decodeAndCallback(QVector<unsigned char> extractedData) = 0;
};



class WriteSingleWordFC6Request : public ModbusRequest
{
    quint16 wordAddress;
    quint16 wordValue;

public:
    WriteSingleWordFC6Request(QModbusTcpClient * client, quint16 transactionId, quint16 wordAddress, quint16 wordValue);

    virtual quint8 getFunctionCode() {
        return 0x06;
    }

    virtual void decodeAndCallback(QVector<unsigned char> extractedData);

    virtual ~WriteSingleWordFC6Request();
};

class ReadMultipleHoldingRegistersFC3Request : public ModbusRequest
{
    quint16 startAddress;
    quint16 nbWord;

public:
    ReadMultipleHoldingRegistersFC3Request(QModbusTcpClient * client, quint16 transactionId, quint16 startAddress, quint16 nbWord);

    virtual quint8 getFunctionCode() {
        return 0x03;
    }

    virtual void decodeAndCallback(QVector<unsigned char> extractedData);

    virtual ~ReadMultipleHoldingRegistersFC3Request();
};

class ReadMultipleInputRegistersFC4Request : public ModbusRequest
{
    quint16 startAddress;
    quint16 nbWord;

public:
    ReadMultipleInputRegistersFC4Request(QModbusTcpClient * client, quint16 transactionId, quint16 startAddress, quint16 nbWord);

    virtual quint8 getFunctionCode() {
        return 0x04;
    }

    virtual void decodeAndCallback(QVector<unsigned char> extractedData);

    virtual ~ReadMultipleInputRegistersFC4Request();
};

class ForceSingleCoilsFC5Request : public ModbusRequest
{
    quint16 coilAddress;
    bool value;

public:
    ForceSingleCoilsFC5Request(QModbusTcpClient * client, quint16 transactionId, quint16 coilAddress, bool values);

    virtual quint8 getFunctionCode() {
        return 0x05;
    }

    virtual void decodeAndCallback(QVector<unsigned char> extractedData);

    virtual ~ForceSingleCoilsFC5Request();
};

class ForceMultipleCoilsFC15Request : public ModbusRequest
{
    quint16 startAddress;
    QVector<bool> values;

public:
    ForceMultipleCoilsFC15Request(QModbusTcpClient * client, quint16 transactionId, quint16 startAddress, QVector<bool> values);

    virtual quint8 getFunctionCode() {
        return 0x0F;
    }

    virtual void decodeAndCallback(QVector<unsigned char> extractedData);

    virtual ~ForceMultipleCoilsFC15Request();
};

class ReadMultipleInputsStatusFC2Request : public ModbusRequest
{
    quint16 startAddress;
    quint16 nbInputs;

public:
    ReadMultipleInputsStatusFC2Request(QModbusTcpClient * client, quint16 transactionId, quint16 startAddress, quint16 nbValues);

    virtual quint8 getFunctionCode() {
        return 0x02;
    }

    virtual void decodeAndCallback(QVector<unsigned char> extractedData);

    virtual ~ReadMultipleInputsStatusFC2Request();
};

class PresetMultipleRegisterFC16Request : public ModbusRequest
{
    quint16 startAddress;
    QVector<quint16> values;

public:
    PresetMultipleRegisterFC16Request(QModbusTcpClient * client, quint16 transactionId, quint16 startAddress, QVector<quint16> values);

    virtual quint8 getFunctionCode() {
        return 0x10;
    }

    virtual void decodeAndCallback(QVector<unsigned char> extractedData);

    virtual ~PresetMultipleRegisterFC16Request();
};


class QModbusTcpClient : public QTcpSocket
{
    Q_OBJECT

    QString host;
    quint16 port;
    quint8 unitId;

    quint16 transactionId;

    QVector<char> buffer;
    QMap<quint16, ModbusRequest*> pendingRequests;

    friend class ReadMultipleInputsStatusFC2Request;
    static bool getBit(char byte, int bitNumber);
    static void setBit(char * byte, int bitNumber, bool value);
    static char getMSB(quint16);
    static char getLSB(quint16);
    quint16 setTransactionId(char * ptr);
    static void setProtocolId(char * ptr);
    static void setLength(char * ptr, quint16 len);
    static void setUnitId(char * ptr, quint8 unitId);
    static void setFunctionCode(char * ptr, quint8 functionCode);

    // Return the transactionId
    quint16 setHeader(char * ptr, quint16 length, quint8 unitId, quint8 functionCode);

    void processModbusSentence();

public:
    explicit QModbusTcpClient(QString host, quint16 port, QObject *parent = nullptr);
    virtual void connectToHost();
    void writeSingleWordFC6(quint16 wordAddress, quint16 wordValue);
    void readMultipleHoldingRegistersFC3(quint16 startAddress, quint16 nbWord);
    void readMultipleInputRegistersFC4(quint16 startAddress, quint16 nbWord);
    void forceSingleCoilFC5(quint16 coilAddress, bool value);
    void forceMultipleCoilsFC15(quint16 startAddress, QVector<bool> values);

    void readMultipleInputsStatusFC2(quint16 startAddress, quint16 nbInput);

    void presetMultipleRegistersFC16(quint16 startAddress, QVector<quint16> values);

signals:
    // FC 06 (0x06)
    void onWriteSingleWordSentence(bool writeSuccess, quint16 wordAddress, quint16 wordValue);

    // FC 03 (0x03)
    void onReadMultipleHoldingRegistersSentence(quint16 startAddress, QVector<quint16> values);
    void onReadMultipleHoldingRegistersSentenceSingleValue(quint16 address, quint16 value);

    // FC 04 (0x04)
    void onReadMultipleInputRegistersSentence(quint16 startAddress, QVector<quint16> values);
    void onReadMultipleInputRegistersSentenceSingleValue(quint16 address, quint16 value);

    // FC 05 (0x05)
    void onForceSingleCoilSentence(bool writeSuccess, quint16 coilAddress, bool value);

    // FC 15 (0x0F)
    void onForceMultipleCoilsSentence(bool writeSuccess, quint16 startAddress, QVector<bool> valuesWriteRequested, quint16 numberOfCoilsWritten);

    // FC 02 (0x02)
    void onReadMultipleInputsStatusSentence(quint16 startAddress, QVector<bool> values);

    // FC 16 (0x10)
    void onPresetMultipleRegistersSentence(bool writeSuccess, quint16 startAddress, QVector<quint16> valuesWriteRequested, quint16 nbValueWritten);

public slots:
    void onDataRecv();

};

#endif // QModbusTcpClient_H

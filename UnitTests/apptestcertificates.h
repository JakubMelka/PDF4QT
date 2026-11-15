// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef APPTESTCERTIFICATES_H
#define APPTESTCERTIFICATES_H

#include <QByteArray>
#include <QString>

namespace pdftest
{

/// Two self-signed public certificates (DER, EC P-256, created for the test by OpenSSL; their
/// private keys were never kept) and their SHA-256 computed by "openssl dgst -sha256" - the rows
/// of the certificate cache of Encrypt with Certificate (step 5 of phase 2, Q5.11) of the storage
/// tests (UnitTestsAppData, UnitTestsAppStorage)
class AppTestCertificates
{
public:
    static QByteArray first()
    {
        return QByteArray::fromBase64("MIIBzDCCAXGgAwIBAgIUE7oeAcUF1twd92BUcDxdnoji4KowCgYIKoZIzj0EAwIwOzEiMCAGA1UEAwwZUERGNFFUIHRlc3QgY2VydGlmaWNhdGUgMTEVMBMGA1UECgwMUERG"
                                      "NFFUIHRlc3RzMB4XDTI2MTExNDE4NDMwN1oXDTM2MTExMTE4NDMwN1owOzEiMCAGA1UEAwwZUERGNFFUIHRlc3QgY2VydGlmaWNhdGUgMTEVMBMGA1UECgwMUERGNFFU"
                                      "IHRlc3RzMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAETdXb07FwozR6q0mXMll90MRpgIQg3fhzIjTqKXhiao5oFdoB5bWwdpPyV75MzxDP0rL3PCkNgQq32XMtv2A1"
                                      "lqNTMFEwHQYDVR0OBBYEFFlP3wK5PgGMbLVhigUR8tzox1LxMB8GA1UdIwQYMBaAFFlP3wK5PgGMbLVhigUR8tzox1LxMA8GA1UdEwEB/wQFMAMBAf8wCgYIKoZIzj0E"
                                      "AwIDSQAwRgIhAOSGRBii07lsCs+N3uWeneDjENBBG/1OD6sg9xzU/eFAAiEAj37rIU+MQHF4tn0N8Wnytkne1wZy+grE1U9cY4paFWU=");
    }

    static QByteArray second()
    {
        return QByteArray::fromBase64("MIIByzCCAXGgAwIBAgIUapqV2GZkU0/vJN2nfSj1+oYtsLcwCgYIKoZIzj0EAwIwOzEiMCAGA1UEAwwZUERGNFFUIHRlc3QgY2VydGlmaWNhdGUgMjEVMBMGA1UECgwMUERG"
                                      "NFFUIHRlc3RzMB4XDTI2MTExNDE4NDMwN1oXDTM2MTExMTE4NDMwN1owOzEiMCAGA1UEAwwZUERGNFFUIHRlc3QgY2VydGlmaWNhdGUgMjEVMBMGA1UECgwMUERGNFFU"
                                      "IHRlc3RzMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAED+DDHZ5ZvzwkFGAXe/Rrk9n4+srnxmtm022WvUjJawAdh5SGRuP54owVf5x5VYLZ5bXJfwelTYtG7MWoKma1"
                                      "s6NTMFEwHQYDVR0OBBYEFOGRjMFJanLLpJ12YZrRF8VCDKVGMB8GA1UdIwQYMBaAFOGRjMFJanLLpJ12YZrRF8VCDKVGMA8GA1UdEwEB/wQFMAMBAf8wCgYIKoZIzj0E"
                                      "AwIDSAAwRQIgE8X60cbCA5mr5sJszK98CyiHHQwdUnCEm8X3pL3DOXUCIQDTaBUElvWeKecTTMetLd3/+0FKmuMx0XYSHLKkIDm7nQ==");
    }

    static QString firstFingerprint() { return QStringLiteral("145799ddc6014bb0dedd7383ba6bf3223ceac98adfc861a7f3faae8e93545e05"); }
    static QString secondFingerprint() { return QStringLiteral("be7de50e311a2ad29a0baa8a0b8baf0d07912da60ec9ff787f34aaedaa5ec093"); }
};

}   // namespace pdftest

#endif // APPTESTCERTIFICATES_H

package top.kenvix.bleunlock

import java.security.KeyPairGenerator
import java.security.Signature
import java.security.spec.ECGenParameterSpec
import java.security.interfaces.ECPublicKey
import java.security.MessageDigest

/** Plain-JVM smoke test: run with kotlinc, without Android dependencies. */
fun main() {
    val nonce = ByteArray(32) { it.toByte() }
    check(Protocol.decodeChallenge(byteArrayOf(1) + nonce)!!.contentEquals(nonce))
    check(Protocol.decodeChallenge(byteArrayOf(2) + nonce) == null)
    check(Protocol.decodeChallenge(byteArrayOf(1) + nonce.dropLast(1)) == null)
    val parts = Protocol.challengeFragments(nonce)
    check(parts.size == 2 && parts[0].size == 17 && parts[1].size == 17)
    check(parts[0][0] == 0x11.toByte() && parts[1][0] == 0x12.toByte())
    check((parts[0].drop(1) + parts[1].drop(1)).toByteArray().contentEquals(nonce))
    check(Protocol.message(nonce).size == 53)
    check(Protocol.message(nonce).copyOfRange(0, 21).toString(Charsets.US_ASCII) ==
        "KENVIX-BLE-UNLOCK/v1:")
    check(Protocol.response(0).contentEquals(byteArrayOf(1, 0, 0)))
    check(Protocol.response(2).contentEquals(byteArrayOf(1, 2, 0)))
    val kp = KeyPairGenerator.getInstance("EC").apply {
        initialize(ECGenParameterSpec("secp256r1"))
    }.generateKeyPair()
    val signature = Signature.getInstance("SHA256withECDSA")
    signature.initSign(kp.private)
    signature.update(Protocol.message(nonce))
    val der = signature.sign()
    check(Protocol.response(1, der).size == der.size + 3)
    val verify = Signature.getInstance("SHA256withECDSA")
    verify.initVerify(kp.public)
    verify.update(Protocol.message(nonce))
    check(verify.verify(der))
    verify.initVerify(kp.public)
    verify.update(Protocol.message(ByteArray(32)))
    check(!verify.verify(der))
    val point = kp.public as ECPublicKey
    fun coord(bytes: ByteArray) = bytes.copyOfRange((bytes.size - 32).coerceAtLeast(0), bytes.size)
        .let { ByteArray(32 - it.size) + it }
    val publicKey = byteArrayOf(4) + coord(point.w.affineX.toByteArray()) + coord(point.w.affineY.toByteArray())
    check(publicKey.size == 65)
    val fingerprint = MessageDigest.getInstance("SHA-256").digest(publicKey).take(8)
        .joinToString("") { "%02X".format(it.toInt() and 0xff) }
    check(Protocol.fingerprint(publicKey) == fingerprint)
    println("Kotlin protocol and ECDSA smoke tests passed")
}

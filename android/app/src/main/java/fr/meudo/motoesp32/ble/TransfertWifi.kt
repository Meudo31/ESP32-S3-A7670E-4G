package fr.meudo.motoesp32.ble

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.net.wifi.WifiNetworkSpecifier
import android.os.Build
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import java.io.ByteArrayOutputStream
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder

/**
 * Gros transferts par le Wi-Fi du module (environ 50 fois plus rapide que le Bluetooth).
 *
 * Le module ouvre son point d'accès « Moto_ESP32 » sur demande (commande WIFI_ON par le Bluetooth, qui
 * renvoie le mot de passe). Le téléphone s'y connecte comme réseau local uniquement : sa connexion
 * Internet reste sur la 4G. Android demande d'accepter ce réseau la première fois.
 * Possible à partir d'Android 10 ; sinon l'appli reste en Bluetooth.
 */
class TransfertWifi(private val context: Context) {

    private val connectivite = context.getSystemService(ConnectivityManager::class.java)
    private var rappel: ConnectivityManager.NetworkCallback? = null
    private var reseau: Network? = null
    private var adresse = "192.168.4.1"

    val disponible: Boolean get() = Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q

    /** Rejoint le Wi-Fi du module ; [reponse] est « nom|mot de passe|adresse ». Renvoie un message d'erreur, ou null. */
    suspend fun rejoindre(reponse: String): String? {
        if (!disponible) return "Wi-Fi direct impossible avant Android 10"
        val morceaux = reponse.split("|")
        if (morceaux.size < 3) return "Réponse du module incomprise"
        adresse = morceaux[2]
        reseau?.let { return null }   // déjà connecté
        val trouve = CompletableDeferred<Network?>()
        val specificateur = WifiNetworkSpecifier.Builder()
            .setSsid(morceaux[0])
            .setWpa2Passphrase(morceaux[1])
            .build()
        val demande = NetworkRequest.Builder()
            .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
            .removeCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
            .setNetworkSpecifier(specificateur)
            .build()
        val nouveauRappel = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(network: Network) { reseau = network; trouve.complete(network) }
            override fun onUnavailable() { trouve.complete(null) }
            override fun onLost(network: Network) { if (reseau == network) reseau = null }
        }
        rappel = nouveauRappel
        connectivite.requestNetwork(demande, nouveauRappel)
        // 60 s : la première fois, Android affiche une fenêtre « Se connecter à Moto_ESP32 ? ».
        val obtenu = withTimeoutOrNull(60_000) { trouve.await() }
        if (obtenu == null) {
            quitter()
            return "Connexion au Wi-Fi du module impossible (refusée, ou partage de connexion actif ?)"
        }
        // Le point d'accès vient de démarrer : on vérifie que le serveur répond.
        repeat(10) {
            if (ping()) return null
            kotlinx.coroutines.delay(500)
        }
        quitter()
        return "Le module ne répond pas sur le Wi-Fi"
    }

    fun quitter() {
        rappel?.let { runCatching { connectivite.unregisterNetworkCallback(it) } }
        rappel = null
        reseau = null
    }

    private fun ouvrir(chemin: String): HttpURLConnection {
        val r = reseau ?: error("Wi-Fi du module non connecté")
        return (r.openConnection(URL("http://$adresse$chemin")) as HttpURLConnection).apply {
            connectTimeout = 5_000
            readTimeout = 30_000
        }
    }

    private suspend fun ping(): Boolean = withContext(Dispatchers.IO) {
        runCatching {
            val c = ouvrir("/ping")
            c.connectTimeout = 2_000; c.readTimeout = 2_000
            val ok = c.responseCode == 200
            c.disconnect(); ok
        }.getOrDefault(false)
    }

    /** Télécharge un fichier de la carte SD ; [progression] reçoit (reçus, total). */
    suspend fun telecharger(chemin: String, progression: (Long, Long) -> Unit): ByteArray? = withContext(Dispatchers.IO) {
        runCatching {
            val c = ouvrir("/fichier?chemin=" + URLEncoder.encode(chemin, "UTF-8"))
            if (c.responseCode != 200) { c.disconnect(); return@runCatching null }
            val total = c.contentLengthLong
            val sortie = ByteArrayOutputStream(if (total > 0) total.toInt() else 65536)
            val tampon = ByteArray(16 * 1024)
            c.inputStream.use { entree ->
                while (true) {
                    val lus = entree.read(tampon)
                    if (lus < 0) break
                    sortie.write(tampon, 0, lus)
                    progression(sortie.size().toLong(), total)
                }
            }
            c.disconnect()
            val octets = sortie.toByteArray()
            if (total > 0 && octets.size.toLong() != total) null else octets
        }.getOrNull()
    }

    /** Envoie un fichier vers la carte SD. Renvoie null si tout va bien, sinon le message d'erreur. */
    suspend fun envoyerUnFichier(chemin: String, contenu: ByteArray, progression: (Long) -> Unit): String? =
        envoyerEnMultipart("/fichier?chemin=" + URLEncoder.encode(chemin, "UTF-8"), chemin.substringAfterLast('/'), contenu, progression)

    /** Envoie un firmware ; le module le vérifie, l'installe et redémarre. */
    suspend fun envoyerUnFirmware(contenu: ByteArray, progression: (Long) -> Unit): String? =
        envoyerEnMultipart("/firmware", "firmware.bin", contenu, progression)

    private suspend fun envoyerEnMultipart(
        chemin: String, nomDuFichier: String, contenu: ByteArray, progression: (Long) -> Unit,
    ): String? = withContext(Dispatchers.IO) {
        runCatching {
            val limite = "----MotoESP32" + System.currentTimeMillis()
            val entete = ("--$limite\r\nContent-Disposition: form-data; name=\"fichier\"; filename=\"$nomDuFichier\"\r\n" +
                "Content-Type: application/octet-stream\r\n\r\n").toByteArray()
            val fin = "\r\n--$limite--\r\n".toByteArray()
            val c = ouvrir(chemin)
            c.requestMethod = "POST"
            c.doOutput = true
            c.readTimeout = 60_000   // le module vérifie le firmware avant de répondre
            c.setRequestProperty("Content-Type", "multipart/form-data; boundary=$limite")
            c.setFixedLengthStreamingMode(entete.size.toLong() + contenu.size + fin.size)
            c.outputStream.use { sortie ->
                sortie.write(entete)
                var position = 0
                while (position < contenu.size) {
                    val n = minOf(16 * 1024, contenu.size - position)
                    sortie.write(contenu, position, n)
                    position += n
                    progression(position.toLong())
                }
                sortie.write(fin)
            }
            val code = c.responseCode
            val message = (if (code == 200) c.inputStream else c.errorStream)?.bufferedReader()?.readText().orEmpty()
            c.disconnect()
            if (code == 200) null else message.ifBlank { "Erreur $code" }
        }.getOrElse { "Liaison Wi-Fi perdue (${it.message})" }
    }
}

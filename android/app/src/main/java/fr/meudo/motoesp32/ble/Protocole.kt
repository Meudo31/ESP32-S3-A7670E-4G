package fr.meudo.motoesp32.ble

import org.json.JSONObject
import java.util.UUID

/**
 * Protocole BLE partagé avec le firmware (include/module_appli.h).
 * Voir docs/protocole_ble.md à la racine du dépôt.
 */
object Protocole {
    const val NOM_MODULE = "Moto_ESP32"

    val SERVICE: UUID = UUID.fromString("7a3b0001-5c1e-4b8a-9f2d-6d6f746f3031")
    val TELEMETRIE: UUID = UUID.fromString("7a3b0002-5c1e-4b8a-9f2d-6d6f746f3031")
    val COMMANDE: UUID = UUID.fromString("7a3b0003-5c1e-4b8a-9f2d-6d6f746f3031")
    val REPONSE: UUID = UUID.fromString("7a3b0004-5c1e-4b8a-9f2d-6d6f746f3031")
    val LOGS: UUID = UUID.fromString("7a3b0005-5c1e-4b8a-9f2d-6d6f746f3031")
    val OTA_CTRL: UUID = UUID.fromString("7a3b0006-5c1e-4b8a-9f2d-6d6f746f3031")
    val OTA_DATA: UUID = UUID.fromString("7a3b0007-5c1e-4b8a-9f2d-6d6f746f3031")

    /** Descripteur standard qui active les notifications. */
    val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

    /** Premier octet de toute image de firmware ESP32. */
    const val OCTET_MAGIQUE_FIRMWARE: Byte = 0xE9.toByte()

    /** Applique un message JSON de télémétrie à l'état courant. Les messages inconnus ou invalides sont ignorés. */
    fun appliquer(etat: Telemetrie, texte: String): Telemetrie {
        val j = try { JSONObject(texte) } catch (e: Exception) { return etat }
        return when (j.optString("t")) {
            "gps" -> etat.copy(gps = EtatGps(
                fix = j.optInt("fix") == 1,
                latitude = j.optDouble("lat", 0.0),
                longitude = j.optDouble("lon", 0.0),
                altitude = j.optDouble("alt", 0.0),
                vitesseKmh = j.optDouble("vit", 0.0),
                cap = j.optDouble("cap", 0.0),
                satellites = j.optInt("sat"),
                hdop = j.optDouble("hdop", 0.0),
                miseAJour = j.optLong("maj"),
            ))
            "bat" -> etat.copy(batterie = EtatBatterie(
                presente = j.optInt("ok") == 1,
                tension = j.optDouble("v", 0.0),
                pourcentage = j.optDouble("pct", 0.0),
            ))
            "moto" -> etat.copy(moto = EtatMoto(
                vitesseKmh = j.optInt("vit"),
                rpm = j.optInt("rpm"),
                rapport = j.optInt("rap"),
                temperatureMoteur = j.optInt("temp"),
                essencePct = j.optInt("ess"),
                contact = j.optInt("contact") == 1,
                bequille = j.optInt("beq") == 1,
                frein = j.optInt("frein") == 1,
                embrayage = j.optInt("embr") == 1,
                abs = j.optInt("abs") == 1,
                clignotantGauche = j.optInt("clg") == 1,
                clignotantDroit = j.optInt("cld") == 1,
                feuxDeRoute = j.optInt("route") == 1,
                feuxDeCroisement = j.optInt("code") == 1,
                warning = j.optInt("warn") == 1,
            ))
            "acc" -> etat.copy(accelerometre = EtatAccelerometre(
                ax = j.optDouble("ax", 0.0),
                ay = j.optDouble("ay", 0.0),
                az = j.optDouble("az", 0.0),
                roulis = j.optDouble("roll", 0.0),
                tangage = j.optDouble("pitch", 0.0),
                forceG = j.optDouble("g", 0.0),
            ))
            "sys" -> etat.copy(systeme = EtatSysteme(
                versionFirmware = j.optString("fw"),
                compilation = j.optString("build"),
                dureeFonctionnementS = j.optLong("up"),
                memoireLibre = j.optLong("heap"),
                psramLibre = j.optLong("psram"),
                partition = j.optString("part"),
            ))
            "4g" -> etat.copy(reseau = EtatReseau(
                etat = j.optInt("etat"),
                signalPct = j.optInt("sig"),
                consommationOctets = j.optLong("conso"),
                restePct = j.optInt("reste"),
            ))
            else -> etat
        }
    }

    /** Lit une réponse à une commande ({"t":"rep",...}), ou null si le texte n'en est pas une. */
    fun lireReponse(texte: String): ReponseCommande? {
        val j = try { JSONObject(texte) } catch (e: Exception) { return null }
        if (j.optString("t") != "rep") return null
        return ReponseCommande(j.optString("cmd"), j.optBoolean("ok"), j.optString("msg"))
    }

    /** Point cardinal (N, NE, E...) pour un cap en degrés. */
    fun cardinal(degres: Double): String {
        val points = arrayOf("N", "NE", "E", "SE", "S", "SO", "O", "NO")
        val index = Math.floorMod(((degres + 22.5) / 45.0).toInt(), 8)
        return points[index]
    }

    /** Durée lisible : 3 j 04 h 12 min, 4 h 05 min, 12 min 30 s. */
    fun duree(secondes: Long): String {
        val j = secondes / 86400
        val h = (secondes % 86400) / 3600
        val m = (secondes % 3600) / 60
        val s = secondes % 60
        return when {
            j > 0 -> "%d j %02d h %02d min".format(j, h, m)
            h > 0 -> "%d h %02d min".format(h, m)
            else -> "%d min %02d s".format(m, s)
        }
    }
}

data class EtatGps(
    val fix: Boolean, val latitude: Double, val longitude: Double, val altitude: Double,
    val vitesseKmh: Double, val cap: Double, val satellites: Int, val hdop: Double, val miseAJour: Long,
)

data class EtatBatterie(val presente: Boolean, val tension: Double, val pourcentage: Double)

data class EtatMoto(
    val vitesseKmh: Int, val rpm: Int, val rapport: Int, val temperatureMoteur: Int, val essencePct: Int,
    val contact: Boolean, val bequille: Boolean, val frein: Boolean, val embrayage: Boolean, val abs: Boolean,
    val clignotantGauche: Boolean, val clignotantDroit: Boolean, val feuxDeRoute: Boolean,
    val feuxDeCroisement: Boolean, val warning: Boolean,
)

data class EtatAccelerometre(
    val ax: Double, val ay: Double, val az: Double, val roulis: Double, val tangage: Double, val forceG: Double,
)

data class EtatSysteme(
    val versionFirmware: String, val compilation: String, val dureeFonctionnementS: Long,
    val memoireLibre: Long, val psramLibre: Long, val partition: String,
)

data class EtatReseau(val etat: Int, val signalPct: Int, val consommationOctets: Long, val restePct: Int)

data class Telemetrie(
    val gps: EtatGps? = null,
    val batterie: EtatBatterie? = null,
    val moto: EtatMoto? = null,
    val accelerometre: EtatAccelerometre? = null,
    val systeme: EtatSysteme? = null,
    val reseau: EtatReseau? = null,
)

data class ReponseCommande(val commande: String, val ok: Boolean, val message: String)

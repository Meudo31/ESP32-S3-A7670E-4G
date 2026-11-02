package fr.meudo.motoesp32

import fr.meudo.motoesp32.ble.Protocole
import fr.meudo.motoesp32.ble.Telemetrie
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** Vérifie la lecture des messages produits par src/module_appli.cpp. */
class ProtocoleTest {

    @Test
    fun lit_un_message_gps() {
        val t = Protocole.appliquer(Telemetrie(),
            """{"t":"gps","fix":1,"lat":43.604652,"lon":1.444209,"alt":146.0,"vit":87.5,"cap":270,"sat":12,"hdop":0.9,"maj":1793600000}""")
        val gps = t.gps!!
        assertTrue(gps.fix)
        assertEquals(43.604652, gps.latitude, 1e-6)
        assertEquals(12, gps.satellites)
        assertEquals("O", Protocole.cardinal(gps.cap))
    }

    @Test
    fun lit_les_donnees_moto() {
        val t = Protocole.appliquer(Telemetrie(),
            """{"t":"moto","vit":92,"rpm":4200,"rap":5,"temp":88,"ess":61,"contact":1,"beq":0,"frein":1,"embr":0,"abs":0,"clg":1,"cld":0,"route":0,"code":1,"warn":0}""")
        val m = t.moto!!
        assertEquals(4200, m.rpm)
        assertEquals(5, m.rapport)
        assertTrue(m.frein && m.clignotantGauche && m.feuxDeCroisement)
    }

    @Test
    fun garde_l_etat_si_message_invalide() {
        val avant = Protocole.appliquer(Telemetrie(), """{"t":"bat","ok":1,"v":3.95,"pct":80.5}""")
        assertEquals(avant, Protocole.appliquer(avant, "pas du json"))
        assertEquals(avant, Protocole.appliquer(avant, """{"t":"inconnu"}"""))
        assertEquals(80.5, avant.batterie!!.pourcentage, 1e-9)
    }

    @Test
    fun lit_une_reponse_de_commande() {
        val r = Protocole.lireReponse("""{"t":"rep","cmd":"PING","ok":true,"msg":"pong"}""")!!
        assertEquals("PING", r.commande)
        assertTrue(r.ok)
        assertNull(Protocole.lireReponse("""{"t":"gps"}"""))
    }

    @Test
    fun points_cardinaux_et_durees() {
        assertEquals("N", Protocole.cardinal(0.0))
        assertEquals("N", Protocole.cardinal(350.0))
        assertEquals("NE", Protocole.cardinal(45.0))
        assertEquals("SO", Protocole.cardinal(225.0))
        assertEquals("2 min 05 s", Protocole.duree(125))
        assertEquals("1 j 01 h 00 min", Protocole.duree(90000))
    }
}

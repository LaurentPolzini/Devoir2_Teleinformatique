import socket
import time
import random
import sys
from pathlib import Path

#import crc
from crcv2 import Frame, encode_frame_to_bytes, decode_frame_from_bytes


LOCALHOST = "127.0.0.1" 

EMITTER_PORT  = 30000 #on peut prendre n'importe quoi selon le pc et les prots dispos
RECEIVER_PORT = 20000

#frame type (commandes)
CMD_DATA = 0
CMD_ACK = 1
CMD_CON_CLOSE = 5  # fin de communication

#settings gobackn
#num_seq un index de trame sur 1 octet (0..255),
# donc on peut envoyer jusqu'à 255 trames sans wrap-around.
N_SEQ  = 256
WINDOW_SIZE  = 10# taille de fenêtre(< N_SEQ evidemment)
DATA_MAX_LEN = 100# max 100 octets par trame

#implémentation canal non fiable (erreurs,pertes,délai)





def send_through_channel(sock: socket.socket,
                         data: bytes,
                         dest_addr,
                         prob_error: float,
                         prob_loss: float,
                         delay_max_ms: int) -> bool:
    """
    Canal non fiable simplifié :

    - prob_loss  : probabilité de perdre complètement la trame.
    - prob_error : probabilité de corrompre la trame en 1 bit.
                   (0.05 => 5% de trames avec un bit flip)
    - delay_max_ms : délai max en millisecondes.
    """

    #trame perdue
    if random.random() < prob_loss:
        # trame perdue, rien envoyé
        return False

    noisy = bytearray(data)

    #corrupted
    if random.random() < prob_error and len(noisy) > 0:
        byte_index = random.randrange(len(noisy))
        bit_index = random.randint(0, 7)
        noisy[byte_index] ^= (1 << bit_index)

    #délai
    if delay_max_ms > 0:
        delay = random.randint(0, delay_max_ms)
        time.sleep(delay / 1000.0)

    sock.sendto(bytes(noisy), dest_addr)
    return True



#utilities

def split_file_into_frames(path: str) -> list[Frame]:
    """
    read un fichier binaire et le coupe en trames DATA de DATA_MAX_LEN octets max.
    num_seq = index de trame (0,1,2,...) tant qu'on ne dépasse pas 255.
    """
    data = Path(path).read_bytes()
    frames: list[Frame] = []
    seq = 0

    for i in range(0, len(data), DATA_MAX_LEN):
        chunk = data[i:i + DATA_MAX_LEN]
        if seq >= 256:
            raise ValueError("Trop de trames pour un num_seq sur 1 octet (>255) something went VERY wrong")
        frames.append(Frame(commande=CMD_DATA, num_seq=seq, info=chunk))
        seq += 1

    return frames


def send_ack(sock: socket.socket,
             dest_addr,
             last_good_seq: int,
             prob_error: float,
             prob_loss: float,
             delay_max_ms: int) -> None:
    """
    Envoie une trame ACK avec num_seq = last_good_seq (ou 255 si aucun).
    """
    if last_good_seq < 0:
        seq_field = 255  #aucune trame encore validée (donc 255 pr etre safe)
    else:
        seq_field = last_good_seq & 0xFF

    ack_frame = Frame(commande=CMD_ACK, num_seq=seq_field, info=b"")
    raw = encode_frame_to_bytes(ack_frame)
    send_through_channel(sock, raw, dest_addr, prob_error, prob_loss, delay_max_ms)


#go back n send (emitter)

def go_back_n_emitter(message_path: str,
                      prob_error: float = 0.0,
                      prob_loss: float = 0.0,
                      delay_max_ms: int = 0,
                      timeout_s: float = 0.3) -> None:
    """
    emitter gobackn:
    decoupe message_path en trames DATA
    envoie avec window
    manage ACK et retransmissions sur timeout
    close avec une trame CON_CLOSE
    """

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((LOCALHOST, EMITTER_PORT))
    sock.settimeout(0.05)  # petits timeouts pour pouvoir vérifier les timers

    dest_addr = (LOCALHOST, RECEIVER_PORT)

    frames = split_file_into_frames(message_path)
    total_frames = len(frames)

    base = 0 # index de la première trame non acquittée
    next_to_send = 0 # index de la prochaine trame à envoyer
    send_time: dict[int, float] = {}

    nb_sent = 0
    nb_retrans = 0
    nb_acks = 0

    t0 = time.time()

    print(f"[EMETTEUR] Fichier '{message_path}' ({total_frames} trames).")
    print(f"[EMETTEUR] Paramètres canal: err={prob_error}, perte={prob_loss}, delaiMax={delay_max_ms} ms\n")

    while base < total_frames:
        #envoi nouvelles trames dans la fenêtre
        while next_to_send < base + WINDOW_SIZE and next_to_send < total_frames:
            frame = frames[next_to_send]
            raw = encode_frame_to_bytes(frame)

            # POV de l'emitter, on a TENTE d'envoyer la trame,
            # donc on démarre le timer et on compte l'envoi, même si le canal perd la trame.
            send_time[next_to_send] = time.time()
            nb_sent += 1

            ok = send_through_channel(sock, raw, dest_addr,
                                    prob_error, prob_loss, delay_max_ms)

            if ok:
                print(f"[EMETTEUR] ENVOI DATA seq={frame.num_seq} (index={next_to_send})")
            else:
                print(f"[EMETTEUR] PERTE (simulation) DATA seq={frame.num_seq}")

            next_to_send += 1

        # reception ack (try)
        try:
            raw_ack, _ = sock.recvfrom(4096)
        except socket.timeout:
            raw_ack = None

        if raw_ack is not None:
            try:
                ack = decode_frame_from_bytes(raw_ack)
            except Exception as e:
                print(f"[EMETTEUR] ACK corrompu ou invalide : {e}")
            else:
                if ack.commande == CMD_ACK:
                    nb_acks += 1
                    ack_seq = ack.num_seq
                    print(f"[EMETTEUR] ACK reçu, last_good_seq={ack_seq}")
                    #if ack_seq est dans l'intervalle [base .. total_frames-1],
                    #then on avance le base a ack_seq + 1
                    if 0 <= ack_seq < total_frames and ack_seq >= base:
                        base = ack_seq + 1
                        #we can also cleanup les send_time obsoletes
                        for i in list(send_time.keys()):
                            if i < base:
                                del send_time[i]

        #gestion du timeout de la trame 'base'
        if base < next_to_send:
            first_index = base
            t_first = send_time.get(first_index)
            if t_first is not None and (time.time() - t_first) > timeout_s:
                print(f"[EMETTEUR] TIMEOUT sur index={first_index}, retransmission depuis index={first_index}")
                # resend toutes les trames de [base .. next_to_send)
                for i in range(base, next_to_send):
                    frame = frames[i]
                    raw = encode_frame_to_bytes(frame)

                    #idem,on considère que la trame est reemise,
                    #donc on met a jour le timer et les compteurs mme si le canal la perd.
                    send_time[i] = time.time()
                    nb_sent += 1
                    nb_retrans += 1

                    ok = send_through_channel(sock, raw, dest_addr,
                                            prob_error, prob_loss, delay_max_ms)

                    if ok:
                        print(f"[EMETTEUR] RE-ENVOI DATA seq={frame.num_seq} (index={i})")
                    else:
                        print(f"[EMETTEUR] PERTE (simulation) RE-ENVOI DATA seq={frame.num_seq}")

    #envoi trame de fermeture CON_CLOSE
    close_frame = Frame(commande=CMD_CON_CLOSE, num_seq=total_frames & 0xFF, info=b"")
    raw_close = encode_frame_to_bytes(close_frame)
    send_through_channel(sock, raw_close, dest_addr,
                         prob_error, prob_loss, delay_max_ms)
    print(f"[EMETTEUR] ENVOI CON_CLOSE seq={close_frame.num_seq}")
    

    elapsed = time.time() - t0
    print("\n[EMETTEUR] Terminé.")
    print(f"Durée totale        : {elapsed:.3f} s")
    print(f"Trames DATA envoyées: {nb_sent}")
    print(f"Retransmissions     : {nb_retrans}")
    print(f"ACK reçus           : {nb_acks}")

    sock.close()


#receiver gobackn

def go_back_n_receiver(output_path: str,
                       prob_error: float = 0.0,
                       prob_loss: float = 0.0,
                       delay_max_ms: int = 0) -> None:
    """
    receiver gobackn :
    receive trames DATA dans l’ordre
    check/verif crc
    reject trames pas dans l'ordre ou corrupted
    massemble les payload dans output_path
    send des ACK avec last_good_seq
    """

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((LOCALHOST, RECEIVER_PORT))
    # pas de timeout ici, on bloque jusqu'à réception

    print(f"[RECEPTEUR] Écriture dans '{output_path}'")
    print(f"[RECEPTEUR] Paramètres canal: err={prob_error}, perte={prob_loss}, delaiMax={delay_max_ms} ms\n")

    t0 = time.time()

    data_chunks: list[bytes] = []
    next_expected_seq = 0
    last_good_seq = -1
    nb_data_ok = 0
    nb_data_corrupted = 0
    nb_ack_sent = 0

    emitter_addr = None


    while True:
        try:
            raw, addr = sock.recvfrom(4096)
        except ConnectionResetError:
            print("[RECEPTEUR] Err: 'Connexion réinitialisée par l'hôte distant',graceful ending")
            break

        if emitter_addr is None:
            emitter_addr = addr

        #try de décoder la trame
        try:
            frame = decode_frame_from_bytes(raw)
        except Exception as e:
            print(f"[RECEPTEUR] Trame corrompue/destuff/CRC invalide: {e}")
            nb_data_corrupted += 1
            #renvoie quand même un ACK du dernier bon
            send_ack(sock, emitter_addr, last_good_seq,
                     prob_error, prob_loss, delay_max_ms)
            nb_ack_sent += 1
            continue

        if frame.commande == CMD_CON_CLOSE:
            print(f"[RECEPTEUR] Reçu CON_CLOSE seq={frame.num_seq}, fermeture.")
            #envoie un dernier ACK
            send_ack(sock, emitter_addr, last_good_seq,
                     prob_error, prob_loss, delay_max_ms)
            nb_ack_sent += 1
            break

        if frame.commande == CMD_DATA:
            print(f"[RECEPTEUR] Reçu DATA seq={frame.num_seq}, attendu={next_expected_seq}")
            if frame.num_seq == next_expected_seq:
                #trame dans l'ordre, CRC déjà validé par decode_frame_from_bytes
                data_chunks.append(frame.info)
                last_good_seq = frame.num_seq
                next_expected_seq += 1
                nb_data_ok += 1
            else:
                #trame pas dans l'ordre, on l'ignore car gobackn
                print(f"[RECEPTEUR] Trame hors ordre (reçue seq={frame.num_seq}, attendu={next_expected_seq}), ignorée.")

            #send ACK du dernier bon
            send_ack(sock, emitter_addr, last_good_seq,
                     prob_error, prob_loss, delay_max_ms)
            nb_ack_sent += 1
        else:
            print(f"[RECEPTEUR] Trame avec commande inconnue/ignorée: {frame.commande}")

    #write le fichier recomposé
    Path(output_path).write_bytes(b"".join(data_chunks))

    elapsed = time.time() - t0
    print("\n[RECEPTEUR] Terminé.")
    print(f"Durée totale         : {elapsed:.3f} s")
    print(f"Trames DATA correctes: {nb_data_ok}")
    print(f"Trames corrompues    : {nb_data_corrupted}")
    print(f"ACK envoyés          : {nb_ack_sent}")

    sock.close()


#main
def main():
    """
    Usage:
    pour récepteur :
    python protocole_py.py recv output.txt [err] [loss] [delay_ms]

    emetteur :
    python protocole_py.py send message.txt [err] [loss] [delay_ms] [timeout_s]

    Ex: (pour le default):
    # Terminal 1 (récepteur)
    python protocole_py.py recv output.txt
    # Terminal 2 (émetteur)
    python protocole_py.py send test.txt
    """
    if len(sys.argv) < 2:
        print(main.__doc__)
        return

    role = sys.argv[1]

    #default settings du canal
    prob_error = 0.05 #proberreur
    prob_loss = 0.10 #probperte
    delay_ms = 200 #delaimax ms

    #on est receveur ou sender
    if role == "recv":
        if len(sys.argv) < 3:
            print("Args pas corrects, usage: python protocole_py.py recv output.txt [err] [loss] [delay_ms]")
            return
        output_path = sys.argv[2]
        if len(sys.argv) > 3:
            prob_error = float(sys.argv[3])
        if len(sys.argv) > 4:
            prob_loss = float(sys.argv[4])
        if len(sys.argv) > 5:
            delay_ms = int(sys.argv[5])

        go_back_n_receiver(output_path, prob_error, prob_loss, delay_ms)

    elif role == "send":
        if len(sys.argv) < 3:
            print("Args pas corrects, usage: python protocole_py.py send message.txt [err] [loss] [delay_ms] [timeout_s]")
            return
        message_path = sys.argv[2]
        timeout_s = 0.3
        if len(sys.argv) > 3:
            prob_error = float(sys.argv[3])
        if len(sys.argv) > 4:
            prob_loss = float(sys.argv[4])
        if len(sys.argv) > 5:
            delay_ms = int(sys.argv[5])
        if len(sys.argv) > 6:
            timeout_s = float(sys.argv[6])

        go_back_n_emitter(message_path, prob_error, prob_loss, delay_ms, timeout_s)

    else:
        print("role invalide, utiliser send ou recv")
        print(main.__doc__)


if __name__ == "__main__":
    main()

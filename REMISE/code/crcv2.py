import sys
from dataclasses import dataclass
from typing import Optional, Tuple


flag = "01111110" # pour apres, flag a ajouter

def byte_to_bits(b: int) -> str:
    """Convertit un octet (0..255) en chaîne de 8 bits."""
    return format(b, "08b")

def bytes_to_bits(data: bytes) -> str:
    """Convertit une séquence d'octets en chaîne de bits."""
    return "".join(f"{b:08b}" for b in data)

def bits_to_bytes(bits: str) -> bytes:
    """
    Convertit une chaîne de bits ('0'/'1') en bytes.
    Si la longueur n'est pas un multiple de 8, on ajoute du padding '0'
    à la fin (car octets)
    """
    # Padding pour aligner à 8 bits
    if len(bits) % 8 != 0:
        padded_len = len(bits) + (8 - (len(bits) % 8))
        bits = bits.ljust(padded_len, "0")

    return bytes(int(bits[i:i+8], 2) for i in range(0, len(bits), 8))


def stuffing(message: str) -> str:
    #message est u
    
    #print("message:",message)
    messageout =[]
    counter = 0
    


    for i in message:
        if i not in ("0", "1"):
            print("")
            raise ValueError("message doit contenir uniquement '0' et '1'")
        messageout.append(i)
        if i == "1":
            counter +=1
            if counter == 5:
                messageout.append("0")
                counter = 0
        else:
            counter = 0

    stuffed_message = "".join(messageout)
    #print("message out:",stuffed_message)
    return stuffed_message



def destuff(message: str) -> str:
    

    #print("destuff in:",message)
    messageout = []
    count_ones = 0
    i = 0
    n = len(message)

    while i < n:
        b = message[i]
        if b not in ("0", "1"):
            print("")
            raise ValueError("message doit contenir uniquement '0' et '1'")

        #add bit courant a la sortie
        messageout.append(b)

        if b == "1":
            count_ones += 1

            if count_ones == 5:
                stuffed_index = i + 1

                #Si pas de bit après les 5 '1' alors erreur
                if stuffed_index >= n:
                    raise ValueError(
                        "Trame corrompue : bit stuffed manquant après 5 bits à '1'"
                    )

                stuffed_bit = message[stuffed_index]#celui d'apres en gros vu que stuffed index est i+1

                #stuffed bit doit etre 0 sinon corruption
                if stuffed_bit != "0":
                    raise ValueError(
                        "Trame corrompue : bit stuffed différent de '0' "
                        f"(reçu '{stuffed_bit}' à index '{stuffed_index}')"
                    )

                #si pas d'erruer: on saute le bit stuffed
                i = stuffed_index # on avance jusqu'au stuffed
                count_ones = 0# on remet le compteur a zéro

        else:
            # reset compter a zero
            count_ones = 0

        i += 1
    #print("destuff out message",messageout)
    return "".join(messageout)


def add_flags(message: str) -> str:#add les flag une fois que stuffed.
    #print("avec flags",flag + message + flag)
    return flag + message + flag


def find_last_flag(message: str) -> int:
    return message.rfind(flag)
    

def remove_flags(bits: str) -> str:
    """Retire delimiter flags."""
    start = bits.find(flag)
    end = bits.rfind(flag)
    if start == -1 or end == -1 or end <= start:
        print("")
        raise ValueError("Flags not found in frame")
    return bits[start + len(flag):end]



def hamming_distance(bits1: str, bits2: str) -> int:
    """
    calcule la distance de Hamming entre deux chaines de bits
    """
    if len(bits1) != len(bits2):
        raise ValueError("Les deux chaînes doivent avoir la même longueur.")
    return sum(c1 != c2 for c1, c2 in zip(bits1, bits2))


#crc16    
def crc16_ccitt(data: bytes, poly: int = 0x1021, init_value: int = 0xFFFF) -> int:
    """
    Calcule le CRC-16 (CCITT) sur 'data'.
    data : seq d'octets (bytes ou bytearray)
    poly : poly gen. (défaut 0x1021)
    init_value : valeur initiale du registre CRC (0xFFFF), certaines versions utilise d'autre chose
    Retourne un entier sur 16 bits (0..65535).
    """
    crc = init_value  # registre 16 bits, changeable selon versions

    for byte in data:
        # On place l'octet qu'on traite dans les 8 bits de poids fort du CRC
        crc ^= (byte << 8)  # equivalent d'ajouter 8 zéros en 

        # parcours chaque bit de l'octet
        for _ in range(8):
            # On verifie le bit de poids fort, si = 1 on branche
            if crc & 0x8000: # si bit de poid fort a 1
                crc = (crc << 1) ^ poly
            else:
                crc <<= 1

            # On garde que 16 bits
            crc &= 0xFFFF

    return crc



#help utils, note, certains ne sont pas utilisés

def bit_stuffing_for_crc16_test(data: bytes) -> str:
    """
    pour tests. 
    Convertit 'data' en bits, applique le bit-stuffing et ajoute les flags.
    Retourne une chaîne de bits.
    """
    bits = bytes_to_bits(data)
    stuffed_bits = stuffing(bits)
    framed_bits = add_flags(stuffed_bits)
    return framed_bits


def crc16_and_stuffing_test(data: bytes) -> None:
    """
    Pour tests CRC+bit-stuffing
    """
    print("Données en bytes :", data)
    print("Données en bits  :", bytes_to_bits(data))

    crc_value = crc16_ccitt(data)
    print(f"CRC-16-CCITT (0x1021) sur les données: 0x{crc_value:04X}")

    frame = data + crc_value.to_bytes(2, "big")
    print("Trame (data+CRC) en bytes :", frame)
    print("Trame (data+CRC) en bits  :", bytes_to_bits(frame))

    stuffed_frame_bits = bit_stuffing_for_crc16_test(frame)
    print("Trame après bit-stuffing + flags :", stuffed_frame_bits)


# HDLC frame builder en CRC16

def build_frame_with_crc16(command: int, numSeq: int, sizePayLoad: int, crc_builtin: int, payload: bytes) -> str:
    """
    Construit latrame HDLC tel que:
    FLAG | stuffing( bits(header + payload + CRC16) ) | FLAG

    Retourne une chaîne de bits avec flags ajoutés.
    """
    #core = en-tête + données (en bytes)
    core = bytes([command & 0xFF, numSeq & 0xFF, sizePayLoad & 0xFF]) + payload

    #CRC-16 sur core
    #crc_value = crc16_ccitt(core)
    #assert(crc_builtin == crc_value)

    #Convertir le CRC en 2 octets vu qu'on la comme un entier (int) et qu'on veut octets
    crc_bytes = crc_builtin.to_bytes(2, byteorder="big", signed=False)

    #core complet: header + payload + CRC
    core_with_crc = core + crc_bytes

    #Convertir en bits
    core_bits = bytes_to_bits(core_with_crc)

    #Bit stuffing
    stuffed_bits = stuffing(core_bits)

    #Ajout flags
    framed_bits = add_flags(stuffed_bits)

    return framed_bits






@dataclass
class Frame:
    """
    representation d'une trame de liaison.
    commande: int -> type de trame (ex: 0=DATA, 1=ACK, etc.)
    num_seq: int-> numéro de séquence (0..N-1)
    info: bytes ->payload
    """
    commande: int
    num_seq: int
    info: bytes

    @property
    def lg_info(self) -> int:
        return len(self.info)


def core_bytes_from_frame(frame: Frame) -> bytes:
    """
    Construit le core d'une trame : header + données (sans CRC).
    Format:[commande (1 octet)] [num_seq (1 octet)] [lg_info (1 octet)] [payload...]
    """
    if frame.lg_info > 255:
        raise ValueError("lg_info ne peut pas dépasser 255 (1 octet).")
    header = bytes([frame.commande & 0xFF,
                    frame.num_seq & 0xFF,
                    frame.lg_info & 0xFF])
    return header + frame.info


def compute_crc16_for_frame(frame: Frame) -> int:
    """
    Calcule le CRC-16-CCITT sur le core de la trame (en-tête + données).
    """
    core = core_bytes_from_frame(frame)
    return crc16_ccitt(core)


def encode_frame_to_bits(frame: Frame) -> str:
    """
    encode frame en flux de bits HDLC (avec stuffing + flags), sous forme de string
    etapes:
    core = en-tête + données
    CRC-16 sur core (2 octets à la fin)
    conversion en bits
    bit-stuffing HDLC sur (core+CRC)
    ajout des flags 01111110 de part et d'autre
    """
    core = core_bytes_from_frame(frame)
    crc_value = compute_crc16_for_frame(frame)
    crc_bytes = crc_value.to_bytes(2, "big")

    core_with_crc = core + crc_bytes
    bits = bytes_to_bits(core_with_crc)

    stuffed_bits = stuffing(bits)
    framed_bits = add_flags(stuffed_bits)
    return framed_bits


def encode_frame_to_bytes(frame: Frame) -> bytes:
    """
    Encode une Frame en suite de bytes prête à être envoyée sur un socket UDP.
    (flags + stuffing + CRC déjà appliqués)
    """
    framed_bits = encode_frame_to_bits(frame)
    return bits_to_bytes(framed_bits)


def decode_frame_from_bits(framed_bits: str) -> Frame:
    """
    Fait l'opération inverse de encode_frame_to_bits :

      1. retire les flags de début/fin
      2. destuff
      3. reconvertit en bytes
      4. sépare en-tête / données / CRC
      5. vérifie le CRC (ValueError si invalid)

    Retourne un objet Frame.
    """
    # 1) Retirer les flags
    core_bits = remove_flags(framed_bits)

    # 2) Destuff
    destuffed_bits = destuff(core_bits)

    # 3) Bits -> bytes
    core_with_crc = bits_to_bytes(destuffed_bits)

    if len(core_with_crc) < 3 + 2:
        raise ValueError("Trame trop courte pour contenir en-tête + CRC.")

    data = core_with_crc[:-2]
    crc_recv = int.from_bytes(core_with_crc[-2:], "big")

    commande = data[0]
    num_seq = data[1]
    lg_info = data[2]

    if len(data) < 3 + lg_info:
        raise ValueError("lg_info incohérent avec la taille des données.")

    payload = data[3:3 + lg_info]

    frame = Frame(commande=commande, num_seq=num_seq, info=payload)

    # Vérification CRC
    crc_calc = compute_crc16_for_frame(frame)
    if crc_calc != crc_recv:
        raise ValueError(
            f"CRC invalide : reçu 0x{crc_recv:04X}, calculé 0x{crc_calc:04X}"
        )

    return frame


def decode_frame_from_bytes(raw: bytes) -> Frame:
    """
    prend bytes recus sur le réseau
    (flags + stuffing + CRC inclus), et renvoie un Frame.
    """
    framed_bits = bytes_to_bits(raw)
    return decode_frame_from_bits(framed_bits)




# if __name__ == "__main__":
#     args = sys.argv[1:]

#     # Si aucun argument => mode test bit-stuffing + CRC
#     if not args:
#         demande = 3
#     else:
#         demande = int(args[0])

#     if demande == 0:
#         comm = int(args[1])
#         numSeq = int(args[2])
#         siz = int(args[3])
#         crc = int(args[4])
#         if siz > 0:
#             framed_bits = build_frame_with_crc16(comm, numSeq, siz, crc, bytes.fromhex(args[5]))
#         else:
#             framed_bits = ""
#         print(framed_bits)

#     elif demande == 1:
#         coreDatas = bytes.fromhex(args[1])
#         print(crc16_ccitt(coreDatas))

#     elif demande == 2:
#         command, seq, size, crc, payload = parse_frame_with_crc16(args[1])
#         print(f"{command}:{seq}:{size}:{crc}:{payload}")

#     elif demande == 3:
#         # Exemple rapide de test local (quand on lance `python crc.py` sans argument)
#         #
#         # 1) Test stuffing/destuff sur le flux fourni dans l'énoncé
#         flux_original = "011111101111101111110111110"
#         print("Flux original :", flux_original)

#         # Après stuffing
#         stuffed = stuffing(flux_original)
#         print("Après stuffing :", stuffed)

#         # Après destuff
#         destuffed = destuff(stuffed)
#         print("Après destuff  :", destuffed)

#         if destuffed == flux_original:
#             print("OK : destuff == flux original")
#         else:
#             print("ERREUR : destuff != flux original")

#         # CRC simple sur un exemple de données
#         exemple_data = b"Bonjour CRC"
#         print("\nExemple CRC-16 sur :", exemple_data)
#         print("CRC-16-CCITT =", hex(crc16_ccitt(exemple_data)))

#         # Test de détection de corruption sur bit stuffed
#         print("\nTest de corruption sur le bit stuffed :")
#         # On refait stuffing pour avoir une trame
#         stuffed_bits = stuffing(flux_original)
#         print("Trame stuffée :", stuffed_bits)

#         # On cherche l'index du premier bit stuffed (0 après 5 '1')
#         count_ones = 0
#         stuffed_zero_index = None
#         for i, bit in enumerate(stuffed_bits):
#             if bit == "1":
#                 count_ones += 1
#             else:
#                 if count_ones == 5:
#                     stuffed_zero_index = i
#                     break
#                 count_ones = 0

#         if stuffed_zero_index is not None:
#             # on corrompt ce bit stuffed (0 -> 1)
#             stuffed_list = list(stuffed_bits)
#             stuffed_list[stuffed_zero_index] = "1"
#             corrupted = "".join(stuffed_list)
#             print("Trame corrompue :", corrupted)

#             try:
#                 destuff(corrupted)
#                 print("ERREUR : la corruption n'a PAS été détectée !")
#             except ValueError as e:
#                 print("Corruption détectée par destuff :", e)
#         else:
#             print("Motif de bit stuffed non trouvé dans la trame (improbable avec cet exemple).")




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
        crc ^= (byte << 8)  # equivalent d'ajouter 8 zéros en binaire 

        # On traite l'octet bit par bit
        for _ in range(8):#8 bits par octet
            # Si le bit de poids fort est à 1
            if crc & 0x8000:
                # On décale à gauche et on applique le polynôme
                crc = ((crc << 1) & 0xFFFF) ^ poly
            else:
                # Sinon on décale juste à gauche
                crc = (crc << 1) & 0xFFFF

    return crc



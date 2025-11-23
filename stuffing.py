#Code pour bit stuffing 

def stuffing(message: str) -> str:
    #message est u
    
    print("message:",message)
    messageout =[]
    counter = 0
    


    for i in message:
        if i not in ("0", "1"):
            raise ValueError("message doit contenir uniquement '0' et '1'")
        messageout.append(i)
        if i == "1":
            counter +=1
            if counter == 5:
                messageout.append(0)
                counter = 0
        if i == "0":
            counter = 0
    return "".join(messageout)


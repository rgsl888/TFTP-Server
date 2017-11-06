/**************************************************************************/

/**                                                                      **/

/**    TFTP SERVER                                                       **/

/**    Assigenment - 3 for ECEN-602 Computer Communication and Networks  **/

/**    Developers : Client - Han Bee Ho                                  **/

/**                 UIN : 826008965                                      **/

/**                 Server - Ramakrishna Prabhu                          **/

/**                 UIN : 725006454                                      **/

/**                                                                      **/

/**************************************************************************/


Both the developers reviewed each other's code and improved the quality of
 it. 
We also helped each other by providing the different test cases to make
 the 
code as robust as possible.


General Info:

-------------


To compile and generate binary just run make command in the source directory as shown below



        make



This will generate a binary


        
         tftps - tftp server




---------------******** Server - Start *******------------------


Description :

-------------



TFTP server supports both the modes (netascii and octet) and 

it supports both RRQ and WRQ as well.



TFTP server is built using fork to handle multiple request 
with 
UDP connection. 



Simple flow of the programs has been depicted


             TFTP Server

           --------------

                 |

               socket

                 |

               bind                Client<-------

                 |               ----|----       |

          ----recvfrom <-------------    ^       |

   Parent |      |                       |       |
 
          -----fork ()                  |       |

                 | Child                 |       |

         if (REQ == RRQ)                 |       |

                 |                       |       |

                 |                       |       |

          False  | True                  |       |

          --------------                 |       |

          |            |                 |       |

          |            |                 |       |
 
     RECV DATA    SEND DATA             |       |

          ^            ^                 |       |

          |            |------------------       |

          |             SEND DATA and RECV ACK   |

          |                                      |

          |       SEND ACK and RECV DATA         |

          ----------------------------------------



Usage :

-------



Syntax : Please run the tftp server as shown below



      ./tftps {PORT_NUMBER} {TFTP_FOLDER_PATH}



      PORT_NUMBER can be from the range of 1025-65536



Example : ./tftps 8080 tftpboot/



---------------******** Server - End *******------------------


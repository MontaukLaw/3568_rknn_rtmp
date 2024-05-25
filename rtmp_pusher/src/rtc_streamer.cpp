#include "yangstream/YangStreamRtc.h"
#include "yangutil/sys/YangLog.h"
#include "yangutil/yangavinfotype.h"
#include "yangrtc/YangPeerConnection.h"

int main(void)
{
    int32_t err = Yang_Ok;
    char *localSdp = NULL;
    char *remoteSdp = NULL;
    yangbool enableWhipWhep = yangtrue;
    YangRtcDirection direction = YangSendonly; // YangSendrecv,YangSendonly,YangRecvonly
    YangPeerConnection *peer = (YangPeerConnection *)yang_calloc(sizeof(YangPeerConnection), 1);
    yang_create_peerConnection(peer);
    peer->addVideoTrack(&peer->peer, Yang_VED_H264);
    peer->addTransceiver(&peer->peer, direction);
    // sfu server
    if (enableWhipWhep)
        err = peer->connectWhipWhepServer(&peer->peer, "http://43.139.145.196/rtc/v1/whip/?app=live&stream=livestream&secret=e8c13b9687ec47f989d80f08b763fdbf");
    else
        err = peer->connectSfuServer(&peer->peer);
    // p2p
    peer->createDataChannel(&peer->peer); // add datachannel
    if ((err = peer->createOffer(&peer->peer, &localSdp)) != Yang_Ok)
    {
        yang_error("createOffer fail!");
        goto cleanup;
    }
    if ((err = peer->setLocalDescription(&peer->peer, localSdp)) != Yang_Ok)
    {
        yang_error("setLocalDescription fail!");
        goto cleanup;
    }
    // get remote peer sdp
    if ((err = peer->setRemoteDescription(&peer->peer, remoteSdp)) != Yang_Ok)
    {
        yang_error("setRemoteDescription fail!");
        goto cleanup;
    }

cleanup:
    return 0;
}
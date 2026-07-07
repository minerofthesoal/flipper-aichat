"""Checks that talking to LM Studio "over Tailscale" is nothing special from
the client's point of view - once WireGuard has a tunnel up, a tailnet IP
(100.64.0.0/10) or a MagicDNS name (`*.ts.net`) is just another reachable
HTTP host. These tests cover the two things that *could* go wrong on the
client side (URL handling, and treating a `.ts.net` host like any other),
plus an opt-in test that hits a real tailnet address if you point one at it.

This sandbox has no tailnet to join, so there's no way to spin up a "fake
Tailscale" for the default test run - the honest way to verify the real
thing is to run it against your own tailnet:

    AI_CHAT_TAILSCALE_URL=http://my-pc.tailxxxx.ts.net:1234 pytest -v -k tailscale

which is exactly what `test_real_tailnet_list_models_if_configured` does.
"""

from __future__ import annotations

import asyncio
import os

import pytest

from ai_chat_tui.lmstudio_client import LmStudioClient

TAILSCALE_URL_ENV = "AI_CHAT_TAILSCALE_URL"


@pytest.mark.parametrize(
    "raw,expected",
    [
        # Bare tailnet IP (Tailscale's CGNAT range), no trailing slash.
        ("http://100.101.102.103:1234", "http://100.101.102.103:1234"),
        # MagicDNS hostname, with a trailing slash a user might paste in.
        ("http://my-pc.tailnet-1234.ts.net:1234/", "http://my-pc.tailnet-1234.ts.net:1234"),
        # `tailscale serve`-style HTTPS hostname on the default port.
        ("https://my-pc.tailnet-1234.ts.net/", "https://my-pc.tailnet-1234.ts.net"),
    ],
)
def test_base_url_handles_tailscale_address_styles(raw, expected):
    client = LmStudioClient(raw)
    assert client.base_url == expected


def test_endpoints_are_built_relative_to_tailscale_host():
    """Nothing in list_models()/stream_chat() hardcodes localhost or assumes
    a particular hostname shape - it's plain string concatenation onto
    whatever base_url was given, so a tailnet address works identically to
    any other host."""
    client = LmStudioClient("http://my-pc.tailnet-1234.ts.net:1234")
    assert client.base_url + "/v1/models" == "http://my-pc.tailnet-1234.ts.net:1234/v1/models"
    assert (
        client.base_url + "/v1/chat/completions"
        == "http://my-pc.tailnet-1234.ts.net:1234/v1/chat/completions"
    )


@pytest.mark.skipif(
    TAILSCALE_URL_ENV not in os.environ,
    reason=f"set {TAILSCALE_URL_ENV}=http://host.tailnet.ts.net:port to run against a real tailnet",
)
def test_real_tailnet_list_models_if_configured():
    """Opt-in integration test: actually reaches out over your tailnet to a
    running LM Studio instance and confirms /v1/models answers. Skipped by
    default (including in CI, which has no tailnet) since it needs a real
    Tailscale-connected LM Studio to point at."""
    base_url = os.environ[TAILSCALE_URL_ENV]
    client = LmStudioClient(base_url, timeout=10.0)
    models = asyncio.run(client.list_models())
    assert isinstance(models, list)
    assert len(models) > 0

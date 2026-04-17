export const disableWifiAction = async (): Promise<void> => {
    try {
        const res = await fetch('/disable-wifi');
        if (!res.ok) {
            throw new Error();
        }
    } catch (error) {
        console.error(error);
    }
};

export const rotateDisplayAction = async (): Promise<void> => {
    try {
        const res = await fetch('/rotate-display');
        if (!res.ok) {
            throw new Error();
        }
    } catch (error) {
        console.error(error);
    }
};

export const refreshDisplayAction = async (): Promise<void> => {
    try {
        const res = await fetch('/refresh-display');
        if (!res.ok) {
            throw new Error();
        }
    } catch (error) {
        console.error(error);
    }
};